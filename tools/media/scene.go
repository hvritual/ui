package main

// A scene bundle is a separate data protocol; v1 Coffee bundles remain unchanged.
import (
	"archive/zip"
	"bytes"
	"crypto/ed25519"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"hash/crc32"
	"image"
	"image/png"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
)

const ScenePixels = 1792 * 1024
const SceneMetadata = 32 * 1024
const ScenePacketMax = 2 * 1024 * 1024

var sceneID = regexp.MustCompile(`^[a-z][a-z0-9_-]{0,31}$`)

type SceneAsset struct {
	ID      string `json:"id"`
	File    string `json:"file"`
	SHA256  string `json:"sha256"`
	License string `json:"license"`
	Width   int    `json:"width"`
	Height  int    `json:"height"`
}
type SceneItem struct {
	Asset  string `json:"asset"`
	HoldMS int    `json:"hold_ms"`
}
type SceneElement struct {
	ID      string      `json:"id"`
	Kind    string      `json:"kind"`
	X       int         `json:"x"`
	Y       int         `json:"y"`
	Width   int         `json:"width"`
	Height  int         `json:"height"`
	Z       int         `json:"z"`
	Visible bool        `json:"visible"`
	Fit     string      `json:"fit"`
	Loop    bool        `json:"loop"`
	Items   []SceneItem `json:"items"`
}
type SceneManifest struct {
	Schema   int            `json:"schema"`
	App      string         `json:"app"`
	Version  string         `json:"version"`
	Assets   []SceneAsset   `json:"assets"`
	Elements []SceneElement `json:"elements"`
}
type sceneTexture struct {
	ID     string `json:"id"`
	Width  int    `json:"width"`
	Height int    `json:"height"`
	Offset int    `json:"offset"`
}
type scenePayload struct {
	Version  string         `json:"version"`
	Assets   []sceneTexture `json:"assets"`
	Elements []SceneElement `json:"elements"`
}
type ValidatedScene struct {
	Manifest SceneManifest
	Packet   []byte
	ID       string
}

func decodeScene(raw []byte) (SceneManifest, error) {
	var m SceneManifest
	if len(raw) > SceneMetadata {
		return m, errors.New("scene metadata budget")
	}
	if e := uniqueJSON(raw); e != nil {
		return m, e
	}
	d := json.NewDecoder(bytes.NewReader(raw))
	d.DisallowUnknownFields()
	e := d.Decode(&m)
	return m, e
}
func sceneShape(m SceneManifest) error {
	if m.Schema != 1 || m.App != "media-scene" || !versionID.MatchString(m.Version) || len(m.Assets) < 1 || len(m.Assets) > 16 || m.Elements == nil || len(m.Elements) > 8 {
		return errors.New("scene schema/app/version/count")
	}
	ids := map[string]bool{}
	pixels := 0
	for _, a := range m.Assets {
		if !sceneID.MatchString(a.ID) || ids[a.ID] || !hexID.MatchString(a.SHA256) || a.License == "" || len(a.License) > 256 {
			return errors.New("scene asset identity/digest/license")
		}
		if a.File != "images/"+a.ID+".png" && a.File != "images/"+a.ID+".jpg" {
			return errors.New("scene image path")
		}
		if a.Width < 16 || a.Width > 512 || a.Height < 16 || a.Height > 512 || a.Width&(a.Width-1) != 0 || a.Height&(a.Height-1) != 0 {
			return errors.New("scene texture dimensions")
		}
		pixels += a.Width * a.Height * 4
		ids[a.ID] = true
	}
	if pixels > ScenePixels {
		return errors.New("scene pixel budget")
	}
	elements := map[string]bool{}
	for _, v := range m.Elements {
		if !sceneID.MatchString(v.ID) || elements[v.ID] || (v.Kind != "image" && v.Kind != "carousel") || v.Z < 0 || v.Z > 100 {
			return errors.New("scene element identity/type/z")
		}
		// Reference coordinates are the protected 1024x416 media content region.
		if v.X < 0 || v.Y < 0 || v.Width < 1 || v.Height < 1 || v.X > 1024-v.Width || v.Y > 416-v.Height {
			return errors.New("scene element outside safe area")
		}
		if v.Fit != "contain" && v.Fit != "stretch" {
			return errors.New("scene fit")
		}
		if len(v.Items) < 1 || len(v.Items) > 16 || (v.Kind == "image" && (len(v.Items) != 1 || v.Loop)) {
			return errors.New("scene playlist")
		}
		for _, p := range v.Items {
			if !ids[p.Asset] || p.HoldMS < 500 || p.HoldMS > 60000 {
				return errors.New("scene missing asset/hold budget")
			}
		}
		elements[v.ID] = true
	}
	return nil
}
func ValidateScene(raw []byte, key ed25519.PublicKey) (*ValidatedScene, error) {
	if len(raw) > MaxBundle || len(key) != ed25519.PublicKeySize {
		return nil, errors.New("scene bundle/key budget")
	}
	z, e := zip.NewReader(bytes.NewReader(raw), int64(len(raw)))
	if e != nil {
		return nil, e
	}
	if len(z.File) < 3 || len(z.File) > 18 {
		return nil, errors.New("scene archive count")
	}
	files := map[string]*zip.File{}
	for _, f := range z.File {
		if files[f.Name] != nil || !f.Mode().IsRegular() || f.UncompressedSize64 > MaxImage || f.Flags&1 != 0 {
			return nil, errors.New("scene archive duplicate/type/size")
		}
		files[f.Name] = f
	}
	read := func(name string, max int64) ([]byte, error) {
		f := files[name]
		if f == nil {
			return nil, errors.New("scene archive missing file")
		}
		r, e := f.Open()
		if e != nil {
			return nil, e
		}
		defer r.Close()
		return limited(r, max)
	}
	manifest, e := read("manifest.json", SceneMetadata)
	if e != nil {
		return nil, e
	}
	sig, e := read("manifest.sig", ed25519.SignatureSize)
	if e != nil {
		return nil, e
	}
	if !ed25519.Verify(key, manifest, sig) {
		return nil, errors.New("scene signature verification failed")
	}
	m, e := decodeScene(manifest)
	if e != nil {
		return nil, e
	}
	if e = sceneShape(m); e != nil {
		return nil, e
	}
	if len(files) != len(m.Assets)+2 {
		return nil, errors.New("scene unexpected files")
	}
	p := scenePayload{Version: m.Version, Elements: m.Elements}
	pixels := []byte{}
	for _, a := range m.Assets {
		encoded, e := read(a.File, MaxImage)
		if e != nil {
			return nil, e
		}
		if hash(encoded) != a.SHA256 {
			return nil, errors.New("scene image hash")
		}
		cfg, format, e := image.DecodeConfig(bytes.NewReader(encoded))
		if e != nil {
			return nil, e
		}
		if (format != "png" && format != "jpeg") || cfg.Width < 1 || cfg.Height < 1 || cfg.Width > 2048 || cfg.Height > 2048 || cfg.Width*cfg.Height > 1<<20 || (format == "png") != strings.HasSuffix(a.File, ".png") {
			return nil, errors.New("scene image format/pixel budget")
		}
		img, _, e := image.Decode(bytes.NewReader(encoded))
		if e != nil {
			return nil, e
		}
		p.Assets = append(p.Assets, sceneTexture{a.ID, a.Width, a.Height, len(pixels)})
		pixels = append(pixels, fitImage(img, a.Width, a.Height).Pix...)
	}
	meta, e := json.Marshal(p)
	if e != nil {
		return nil, e
	}
	if len(meta) > SceneMetadata {
		return nil, errors.New("scene packet metadata budget")
	}
	payload := append(meta, pixels...)
	head := make([]byte, 32)
	copy(head, []byte("PUISCNE1"))
	binary.LittleEndian.PutUint32(head[8:], 1)
	binary.LittleEndian.PutUint32(head[12:], uint32(len(meta)))
	binary.LittleEndian.PutUint32(head[16:], uint32(len(pixels)))
	binary.LittleEndian.PutUint32(head[20:], crc32.ChecksumIEEE(payload))
	return &ValidatedScene{m, append(head, payload...), hash(raw)}, nil
}
func SceneInstall(dir string, raw []byte, key ed25519.PublicKey) (*ValidatedScene, error) {
	v, e := ValidateScene(raw, key)
	if e != nil {
		return nil, e
	}
	if e = ensureStore(dir); e != nil {
		return nil, e
	}
	unlock, e := storeLock(dir)
	if e != nil {
		return nil, e
	}
	defer unlock()
	receipt, e := json.Marshal(v.Manifest)
	if e != nil {
		return nil, e
	}
	e = activateFiles(dir, v.ID, v.Packet, receipt, raw)
	return v, e
}
func SceneRollback(dir string, key ed25519.PublicKey) (*ValidatedScene, error) {
	if e := ensureStore(dir); e != nil {
		return nil, e
	}
	unlock, e := storeLock(dir)
	if e != nil {
		return nil, e
	}
	defer unlock()
	_, prev, e := state(dir)
	if e != nil {
		return nil, e
	}
	if prev == "" || prev == "-" {
		return nil, errors.New("no scene rollback generation")
	}
	raw, e := regular(filepath.Join(dir, prev+".bundle"), MaxBundle)
	if e != nil {
		return nil, e
	}
	v, e := ValidateScene(raw, key)
	if e != nil {
		return nil, e
	}
	if v.ID != prev {
		return nil, errors.New("scene rollback hash")
	}
	receipt, e := json.Marshal(v.Manifest)
	if e != nil {
		return nil, e
	}
	e = activateFiles(dir, v.ID, v.Packet, receipt, raw)
	return v, e
}
func PackScene(dir string, config []byte, key ed25519.PrivateKey) ([]byte, error) {
	m, e := decodeScene(config)
	if e != nil {
		return nil, e
	}
	// Validate paths and budgets before opening source files; fill only digests.
	for i := range m.Assets {
		m.Assets[i].SHA256 = strings.Repeat("0", 64)
	}
	if e = sceneShape(m); e != nil {
		return nil, e
	}
	files := map[string][]byte{}
	for i, a := range m.Assets {
		b, e := regular(filepath.Join(dir, a.File), MaxImage)
		if e != nil {
			return nil, e
		}
		m.Assets[i].SHA256 = hash(b)
		files[a.File] = b
	}
	raw, e := json.Marshal(m)
	if e != nil {
		return nil, e
	}
	files["manifest.json"] = raw
	files["manifest.sig"] = ed25519.Sign(key, raw)
	var buffer bytes.Buffer
	z := zip.NewWriter(&buffer)
	names := []string{}
	for k := range files {
		names = append(names, k)
	}
	sort.Strings(names)
	for _, k := range names {
		h := &zip.FileHeader{Name: k, Method: zip.Deflate}
		h.SetMode(0600)
		w, e := z.CreateHeader(h)
		if e != nil {
			return nil, e
		}
		if _, e = w.Write(files[k]); e != nil {
			return nil, e
		}
	}
	if e = z.Close(); e != nil {
		return nil, e
	}
	if _, e = ValidateScene(buffer.Bytes(), key.Public().(ed25519.PublicKey)); e != nil {
		return nil, e
	}
	return buffer.Bytes(), nil
}
func sceneCommand(a []string) error {
	switch a[0] {
	case "scene-fixture":
		if len(a) != 2 {
			break
		}
		if e := os.MkdirAll(filepath.Join(a[1], "images"), 0700); e != nil {
			return e
		}
		for i := 0; i < 9; i++ {
			w, h := 128, 64
			if i == 0 || i == 2 || i == 6 {
				w, h = 512, 256
			}
			var b bytes.Buffer
			if e := png.Encode(&b, fixtureImageSize(i%8, 0, w, h)); e != nil {
				return e
			}
			if e := os.WriteFile(filepath.Join(a[1], "images", fmt.Sprintf("campaign_%d.png", i)), b.Bytes(), 0600); e != nil {
				return e
			}
		}
		return nil
	case "scene-pack":
		if len(a) != 5 {
			break
		}
		config, e := regular(a[2], SceneMetadata)
		if e != nil {
			return e
		}
		key, e := privateKey(a[3])
		if e != nil {
			return e
		}
		b, e := PackScene(a[1], config, key)
		if e != nil {
			return e
		}
		return writeNew(a[4], b)
	case "scene-install", "scene-fetch":
		if len(a) != 4 {
			break
		}
		key, e := publicKey(a[2])
		if e != nil {
			return e
		}
		var raw []byte
		if a[0] == "scene-install" {
			raw, e = regular(a[3], MaxBundle)
		} else {
			raw, e = download(a[3], HTTPSClient())
		}
		if e != nil {
			return e
		}
		v, e := SceneInstall(a[1], raw, key)
		if e != nil {
			return e
		}
		fmt.Printf("SCENE_INSTALLED version=%s generation=%s applied=false\n", v.Manifest.Version, v.ID)
		return nil
	case "scene-rollback":
		if len(a) != 3 {
			break
		}
		key, e := publicKey(a[2])
		if e != nil {
			return e
		}
		v, e := SceneRollback(a[1], key)
		if e != nil {
			return e
		}
		fmt.Printf("SCENE_ROLLBACK version=%s generation=%s applied=false\n", v.Manifest.Version, v.ID)
		return nil
	}
	return errors.New("usage: mediactl scene-pack ROOT SCENE_JSON PRIVATE_KEY OUTPUT | scene-install STORE PUBLIC_KEY FILE | scene-fetch STORE PUBLIC_KEY HTTPS_URL | scene-rollback STORE PUBLIC_KEY; status STORE is shared")
}
