package main

import (
	"crypto/ed25519"
	"encoding/binary"
	"encoding/json"
	"hash/crc32"
	"image/png"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func testScene(t *testing.T, version string, key ed25519.PrivateKey) []byte {
	t.Helper()
	root := t.TempDir()
	os.Mkdir(filepath.Join(root, "images"), 0700)
	m := SceneManifest{Schema: 1, App: "media-scene", Version: version, Elements: []SceneElement{{ID: "banner", Kind: "carousel", X: 16, Y: 16, Width: 900, Height: 320, Visible: true, Fit: "contain", Loop: true, Items: []SceneItem{{"campaign", 1000}, {"ninth_image", 2000}}}}}
	for _, id := range []string{"campaign", "ninth_image"} {
		p := filepath.Join(root, "images", id+".png")
		f, e := os.Create(p)
		if e != nil {
			t.Fatal(e)
		}
		png.Encode(f, fixtureImage(2, 0))
		f.Close()
		m.Assets = append(m.Assets, SceneAsset{ID: id, File: "images/" + id + ".png", License: "test-authored", Width: 256, Height: 128})
	}
	b, _ := json.Marshal(m)
	raw, e := PackScene(root, b, key)
	if e != nil {
		t.Fatal(e)
	}
	return raw
}
func TestSceneRoundtrip(t *testing.T) {
	pub, key := testKey(t)
	raw := testScene(t, "new-scene", key)
	v, e := ValidateScene(raw, pub)
	if e != nil {
		t.Fatal(e)
	}
	n := binary.LittleEndian.Uint32(v.Packet[12:])
	pixels := binary.LittleEndian.Uint32(v.Packet[16:])
	if string(v.Packet[:8]) != "PUISCNE1" || int(n+pixels+32) != len(v.Packet) || binary.LittleEndian.Uint32(v.Packet[20:]) != crc32.ChecksumIEEE(v.Packet[32:]) || len(v.Packet) > ScenePacketMax {
		t.Fatal("bad packet")
	}
	var p scenePayload
	if e = json.Unmarshal(v.Packet[32:32+n], &p); e != nil || len(p.Assets) != 2 || p.Assets[1].ID != "ninth_image" {
		t.Fatal("fixed Coffee ids leaked", e)
	}
	if _, e = Validate(raw, pub); e == nil {
		t.Fatal("scene accepted as Coffee")
	}
	if _, e = ValidateScene(testPack(t, "old", key), pub); e == nil {
		t.Fatal("Coffee accepted as scene")
	}
}
func TestSceneRejectSignedInvalid(t *testing.T) {
	pub, key := testKey(t)
	raw := testScene(t, "valid", key)
	for _, kind := range []string{"traversal", "duplicate-id", "unknown-field", "missing-ref", "bad-duration", "budget", "geometry", "video", "duplicate-key", "code-file"} {
		t.Run(kind, func(t *testing.T) {
			bad := rewrite(t, raw, func(files map[string][]byte) {
				var m SceneManifest
				json.Unmarshal(files["manifest.json"], &m)
				switch kind {
				case "traversal":
					m.Assets[0].File = "../escape.png"
				case "duplicate-id":
					m.Assets[1].ID = m.Assets[0].ID
				case "missing-ref":
					m.Elements[0].Items[0].Asset = "missing"
				case "bad-duration":
					m.Elements[0].Items[0].HoldMS = 0
				case "budget":
					for i := range m.Assets {
						m.Assets[i].Width = 512
						m.Assets[i].Height = 512
					}
				case "geometry":
					m.Elements[0].Y = 415
				case "video":
					m.Elements[0].Kind = "video"
				case "code-file":
					files["app.js"] = []byte("eval('bad')")
				}
				b, _ := json.Marshal(m)
				if kind == "unknown-field" {
					b = append([]byte(`{"script":"bad",`), b[1:]...)
				}
				if kind == "duplicate-key" {
					b = append([]byte(`{"schema":1,`), b[1:]...)
				}
				files["manifest.json"] = b
				files["manifest.sig"] = ed25519.Sign(key, b)
			})
			if _, e := ValidateScene(bad, pub); e == nil {
				t.Fatal("invalid accepted")
			}
		})
	}
	other, _ := testKey(t)
	if _, e := ValidateScene(raw, other); e == nil {
		t.Fatal("wrong signer")
	}
	bad := rewrite(t, raw, func(files map[string][]byte) { files["images/campaign.png"][5] ^= 1 })
	if _, e := ValidateScene(bad, pub); e == nil {
		t.Fatal("pixel tamper")
	}
}
func TestSceneInstallRollback(t *testing.T) {
	pub, key := testKey(t)
	root := filepath.Join(t.TempDir(), "scene-store")
	a := testScene(t, "a", key)
	b := testScene(t, "b", key)
	va, e := SceneInstall(root, a, pub)
	if e != nil {
		t.Fatal(e)
	}
	vb, e := SceneInstall(root, b, pub)
	if e != nil {
		t.Fatal(e)
	}
	cur, prev, e := state(root)
	if e != nil || cur != vb.ID || prev != va.ID {
		t.Fatal("current/previous")
	}
	if _, e = SceneInstall(root, []byte("bad"), pub); e == nil {
		t.Fatal("invalid installed")
	}
	c, p, _ := state(root)
	if cur != c || prev != p {
		t.Fatal("bad install changed pointers")
	}
	r, e := SceneRollback(root, pub)
	if e != nil || r.ID != va.ID {
		t.Fatal("rollback", e)
	}
	os.WriteFile(filepath.Join(root, vb.ID+".bundle"), []byte("bad"), 0600)
	if _, e = SceneRollback(root, pub); e == nil {
		t.Fatal("corrupt rollback accepted")
	}
}
func TestSceneShapesAndEmptySnapshot(t *testing.T) {
	_, key := testKey(t)
	pub := key.Public().(ed25519.PublicKey)
	raw := testScene(t, "shape", key)
	v, _ := ValidateScene(raw, pub)
	m := v.Manifest
	m.Elements = []SceneElement{}
	if e := sceneShape(m); e != nil {
		t.Fatal("cannot remove all nodes", e)
	}
	m.Assets[0].Width = 300
	if e := sceneShape(m); e == nil {
		t.Fatal("non power-of-two texture")
	}
	m.Assets[0].Width = 256
	m.Assets[0].ID = "constructor"
	m.Assets[0].File = "images/constructor.png"
	if e := sceneShape(m); e != nil {
		t.Fatal("Map-safe identifier rejected", e)
	}
	for i := 0; i < 17; i++ {
		m.Assets = append(m.Assets, SceneAsset{ID: strings.Repeat("a", i+1), SHA256: strings.Repeat("0", 64), Width: 16, Height: 16, License: "test"})
	}
	if e := sceneShape(m); e == nil {
		t.Fatal("unbounded registry")
	}
}

func TestSceneStoreCannotMixCoffee(t *testing.T) {
	pub, key := testKey(t)
	raw := testScene(t, "scene", key)
	coffee := testPack(t, "coffee", key)
	a, b := filepath.Join(t.TempDir(), "a"), filepath.Join(t.TempDir(), "b")
	if _, e := Install(a, coffee, pub); e != nil {
		t.Fatal(e)
	}
	if _, e := SceneInstall(a, raw, pub); e == nil {
		t.Fatal("scene mixed into Coffee store")
	}
	if _, e := SceneInstall(b, raw, pub); e != nil {
		t.Fatal(e)
	}
	if _, e := Install(b, coffee, pub); e == nil {
		t.Fatal("Coffee mixed into scene store")
	}
}
