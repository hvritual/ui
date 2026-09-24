package main

// Signed local standby playlist; executable code, URLs and business actions are forbidden.
// Actual H264 frame admission is independently performed by the isolated decoder.
import (
 "archive/zip"
 "bytes"
 "crypto/ed25519"
 "encoding/binary"
 "encoding/json"
 "errors"
 "fmt"
 "image"
 "os"
 "path/filepath"
 "sort"
 "strings"
)
const VideoFileMax=8<<20
const VideoAssetLimit=8
const VideoItemLimit=16
type VideoAsset struct {
 ID string `json:"id"`
 File string `json:"file"`
 SHA256 string `json:"sha256"`
 License string `json:"license"`
 Kind string `json:"kind"`
 Width int `json:"width"`
 Height int `json:"height"`
 FPSNum int `json:"fps_num"`
 FPSDen int `json:"fps_den"`
}
type VideoItem struct { Asset string `json:"asset"`; HoldMS int `json:"hold_ms"` }
type VideoManifest struct {
 Schema int `json:"schema"`
 App string `json:"app"`
 Version string `json:"version"`
 IdleMS int `json:"idle_ms"`
 Loop bool `json:"loop"`
 Poster string `json:"poster"`
 Assets []VideoAsset `json:"assets"`
 Items []VideoItem `json:"items"`
}
type ValidatedVideo struct { Manifest VideoManifest; ID string; Index []byte; Payloads [][]byte }
func decodeVideo(raw []byte)(VideoManifest,error) {
 var m VideoManifest
 if len(raw)>32<<10{return m,errors.New("video manifest size")}
 if e:=uniqueJSON(raw);e!=nil{return m,e}
 d:=json.NewDecoder(bytes.NewReader(raw));d.DisallowUnknownFields();e:=d.Decode(&m)
 if e!=nil{return m,e}
 if m.Schema!=1||m.App!="standby-video"||!versionID.MatchString(m.Version)||m.IdleMS<1000||m.IdleMS>60000||len(m.Assets)<2||len(m.Assets)>VideoAssetLimit||len(m.Items)<1||len(m.Items)>VideoItemLimit{return m,errors.New("video schema or budget")}
 ids:=map[string]VideoAsset{};videoCount:=0
 for _,a:=range m.Assets {
  if !sceneID.MatchString(a.ID)||ids[a.ID].ID!=""||!hexID.MatchString(a.SHA256)||a.License==""||len(a.License)>256||a.Width<16||a.Height<16||a.Width>640||a.Height>360{return m,errors.New("video asset shape")}
  switch a.Kind {
  case "video":
   if a.File!="videos/"+a.ID+".mp4"||a.Width%2!=0||a.Height%2!=0||a.FPSNum<1||a.FPSNum>30000||a.FPSDen<1||a.FPSDen>1001||a.FPSNum>30*a.FPSDen{return m,errors.New("video codec specification")};videoCount++
  case "image":
   if (a.File!="images/"+a.ID+".png"&&a.File!="images/"+a.ID+".jpg")||a.FPSNum!=0||a.FPSDen!=0{return m,errors.New("video still specification")}
  default:return m,errors.New("video asset kind")
  }
  ids[a.ID]=a
 }
 if videoCount==0||ids[m.Poster].Kind!="image"{return m,errors.New("video and poster required")}
 for _,p:=range m.Items {
  a,ok:=ids[p.Asset];if !ok{return m,errors.New("video playlist reference")}
  if a.Kind=="image"&&(p.HoldMS<500||p.HoldMS>60000)||a.Kind=="video"&&p.HoldMS!=0{return m,errors.New("video playlist dwell")}
 }
 return m,nil
}
func ValidateVideo(raw []byte,key ed25519.PublicKey)(*ValidatedVideo,error) {
 if len(raw)>MaxBundle||len(key)!=ed25519.PublicKeySize{return nil,errors.New("video bundle/key budget")}
 z,e:=zip.NewReader(bytes.NewReader(raw),int64(len(raw)));if e!=nil{return nil,e}
 if len(z.File)<4||len(z.File)>VideoAssetLimit+2{return nil,errors.New("video ZIP count")}
 files:=map[string]*zip.File{}
 for _,f:=range z.File {
  if files[f.Name]!=nil||!f.Mode().IsRegular()||f.Flags&1!=0||f.UncompressedSize64>VideoFileMax{return nil,errors.New("video ZIP type/size/duplicate")};files[f.Name]=f
 }
 read:=func(name string,max int64)([]byte,error){f:=files[name];if f==nil{return nil,errors.New("video ZIP missing file")};r,e:=f.Open();if e!=nil{return nil,e};defer r.Close();return limited(r,max)}
 meta,e:=read("manifest.json",32<<10);if e!=nil{return nil,e};sig,e:=read("manifest.sig",ed25519.SignatureSize);if e!=nil{return nil,e}
 if !ed25519.Verify(key,meta,sig){return nil,errors.New("video signature")}
 m,e:=decodeVideo(meta);if e!=nil{return nil,e};if len(files)!=len(m.Assets)+2{return nil,errors.New("video extra file")}
 v:=&ValidatedVideo{Manifest:m,ID:hash(raw)}
 index:=make([]byte,64+112*len(m.Assets)+8*len(m.Items));copy(index,[]byte("PUIVPL1\x00"))
 put:=func(off int,n int){binary.LittleEndian.PutUint32(index[off:],uint32(n))}
 put(8,1);put(12,len(m.Assets));put(16,len(m.Items));if m.Loop{put(20,1)};put(24,m.IdleMS)
 ids:=map[string]int{};total:=0
 for i,a:=range m.Assets {
  b,e:=read(a.File,VideoFileMax);if e!=nil{return nil,e};total+=len(b);if total>MaxBundle{return nil,errors.New("video aggregate source budget")}
  if hash(b)!=a.SHA256{return nil,errors.New("video asset hash")}
  payload:=b;kind:=2
  if a.Kind=="image" {
   kind=1
   if len(b)>MaxImage{return nil,errors.New("video image size")}
   cfg,format,e:=image.DecodeConfig(bytes.NewReader(b));if e!=nil{return nil,e}
   if cfg.Width<1||cfg.Height<1||cfg.Width>2048||cfg.Height>2048||cfg.Width*cfg.Height>1<<20||(format!="png"&&format!="jpeg")||(format=="png")!=strings.HasSuffix(a.File,".png"){return nil,errors.New("video poster pixel format")}
   img,_,e:=image.Decode(bytes.NewReader(b));if e!=nil{return nil,e};out:=fitImage(img,a.Width,a.Height)
   payload=make([]byte,a.Width*a.Height*4)
   for y:=0;y<a.Height;y++{for x:=0;x<a.Width;x++{s:=out.NRGBAAt(x,y);o:=(y*a.Width+x)*4;payload[o]=byte((uint32(s.B)*uint32(s.A)+127)/255);payload[o+1]=byte((uint32(s.G)*uint32(s.A)+127)/255);payload[o+2]=byte((uint32(s.R)*uint32(s.A)+127)/255);payload[o+3]=255}}
  } else {
   if len(b)<12||string(b[4:8])!="ftyp"{return nil,errors.New("video requires MP4 ftyp")}
  }
  off:=64+i*112;put(off,kind);put(off+4,a.Width);put(off+8,a.Height);put(off+12,a.FPSNum);put(off+16,a.FPSDen);put(off+20,len(payload))
  copy(index[off+32:off+96],hash(payload));ids[a.ID]=i;v.Payloads=append(v.Payloads,payload)
 }
 put(28,ids[m.Poster])
 for i,p:=range m.Items{off:=64+112*len(m.Assets)+8*i;put(off,ids[p.Asset]);put(off+4,p.HoldMS)}
 copy(index[32:64],[]byte(m.Version))
 v.Index=index;return v,nil
}
func videoStoreKind(dir string)error {
 if e:=ensureStore(dir);e!=nil{return e}
 kind,e:=regular(filepath.Join(dir,"kind"),32)
 if e==nil {if string(kind)!="standby-video\n"{return errors.New("video store kind")};return nil}
 if !os.IsNotExist(e){return e}
 entries,e:=os.ReadDir(dir);if e!=nil{return e}
 for _,f:=range entries {if f.Name()!=".lock"{return errors.New("video requires separate empty store")}}
 return atomicWrite(dir,"kind",[]byte("standby-video\n"))
}
func activateVideo(dir string,v *ValidatedVideo,raw []byte)error {
 if e:=videoStoreKind(dir);e!=nil{return e}
 current,_,e:=state(dir);if e!=nil{return e}
 dest:=filepath.Join(dir,v.ID)
 if _,e=os.Lstat(dest);os.IsNotExist(e) {
  temp,e:=os.MkdirTemp(dir,".video-pending-");if e!=nil{return e};defer os.RemoveAll(temp)
  if e=atomicWrite(temp,"index",v.Index);e!=nil{return e}
  if e=atomicWrite(temp,"original.bundle",raw);e!=nil{return e}
  for i,p:=range v.Payloads{if e=atomicWrite(temp,fmt.Sprintf("asset-%d",i),p);e!=nil{return e}}
  if e=os.Rename(temp,dest);e!=nil{return e};if e=syncDir(dir);e!=nil{return e}
 } else if e!=nil{return e} else {
  old,e:=regular(filepath.Join(dest,"index"),4096);if e!=nil||!bytes.Equal(old,v.Index){return errors.New("existing video generation corrupt")}
  for i,p:=range v.Payloads{old,e:=regular(filepath.Join(dest,fmt.Sprintf("asset-%d",i)),VideoFileMax);if e!=nil||!bytes.Equal(old,p){return errors.New("existing video payload corrupt")}}
 }
 if current==v.ID{return nil};if current==""{current="-"}
 if e=atomicWrite(dir,"current",[]byte(v.ID+"\n"+current+"\n"));e!=nil{return e}
 entries,e:=os.ReadDir(dir);if e!=nil{return e}
 for _,f:=range entries{if hexID.MatchString(f.Name())&&f.Name()!=v.ID&&f.Name()!=current{if e=os.RemoveAll(filepath.Join(dir,f.Name()));e!=nil{return e}}}
 return syncDir(dir)
}
func InstallVideo(dir string,raw []byte,key ed25519.PublicKey)(*ValidatedVideo,error) {
 v,e:=ValidateVideo(raw,key);if e!=nil{return nil,e};if e=ensureStore(dir);e!=nil{return nil,e}
 unlock,e:=storeLock(dir);if e!=nil{return nil,e};defer unlock();if e=activateVideo(dir,v,raw);e!=nil{return nil,e};return v,nil
}
func RollbackVideo(dir string,key ed25519.PublicKey)(*ValidatedVideo,error) {
 if e:=ensureStore(dir);e!=nil{return nil,e};unlock,e:=storeLock(dir);if e!=nil{return nil,e};defer unlock()
 if e=videoStoreKind(dir);e!=nil{return nil,e};_,prev,e:=state(dir);if e!=nil{return nil,e};if !hexID.MatchString(prev){return nil,errors.New("no previous video")}
 raw,e:=regular(filepath.Join(dir,prev,"original.bundle"),MaxBundle);if e!=nil{return nil,e};v,e:=ValidateVideo(raw,key);if e!=nil{return nil,e}
 if v.ID!=prev{return nil,errors.New("video rollback identity")};if e=activateVideo(dir,v,raw);e!=nil{return nil,e};return v,nil
}
func PackVideo(root string,raw []byte,key ed25519.PrivateKey)([]byte,error) {
 var m VideoManifest
 if e:=uniqueJSON(raw);e!=nil{return nil,e};d:=json.NewDecoder(bytes.NewReader(raw));d.DisallowUnknownFields();if e:=d.Decode(&m);e!=nil{return nil,e}
 if len(m.Assets)>VideoAssetLimit{return nil,errors.New("video assets")}
 files:=map[string][]byte{}
 for i:=range m.Assets {m.Assets[i].SHA256=strings.Repeat("0",64)}
 shape,e:=json.Marshal(m);if e!=nil{return nil,e};if _,e=decodeVideo(shape);e!=nil{return nil,e}
 for i:=range m.Assets{a:=&m.Assets[i];b,e:=regular(filepath.Join(root,filepath.FromSlash(a.File)),VideoFileMax);if e!=nil{return nil,e};a.SHA256=hash(b);files[a.File]=b}
 meta,e:=json.Marshal(m);if e!=nil{return nil,e};files["manifest.json"]=meta;files["manifest.sig"]=ed25519.Sign(key,meta)
 var out bytes.Buffer;z:=zip.NewWriter(&out);names:=[]string{};for n:=range files{names=append(names,n)};sort.Strings(names)
 for _,n:=range names{h:=&zip.FileHeader{Name:n,Method:zip.Store};h.SetMode(0600);w,e:=z.CreateHeader(h);if e!=nil{return nil,e};if _,e=w.Write(files[n]);e!=nil{return nil,e}}
 if e=z.Close();e!=nil{return nil,e};if _,e=ValidateVideo(out.Bytes(),key.Public().(ed25519.PublicKey));e!=nil{return nil,e};return out.Bytes(),nil
}
func videoCommand(a []string)error {
 switch a[0] {
 case "video-pack":
  if len(a)!=5{break};raw,e:=regular(a[2],32<<10);if e!=nil{return e};key,e:=privateKey(a[3]);if e!=nil{return e};bundle,e:=PackVideo(a[1],raw,key);if e!=nil{return e};return writeNew(a[4],bundle)
 case "video-install","video-fetch":
  if len(a)!=4{break};key,e:=publicKey(a[2]);if e!=nil{return e};var raw []byte
  if a[0]=="video-fetch"{raw,e=download(a[3],HTTPSClient())}else{raw,e=regular(a[3],MaxBundle)}
  if e!=nil{return e};v,e:=InstallVideo(a[1],raw,key);if e!=nil{return e};fmt.Printf("VIDEO_INSTALLED generation=%s applied=false decoder_admission=pending\n",v.ID);return nil
 case "video-rollback":
  if len(a)!=3{break};key,e:=publicKey(a[2]);if e!=nil{return e};v,e:=RollbackVideo(a[1],key);if e!=nil{return e};fmt.Printf("VIDEO_ROLLBACK generation=%s applied=false\n",v.ID);return nil
 }
 return errors.New("video-pack ROOT MANIFEST PRIVATE OUTPUT | video-install STORE PUBLIC ZIP | video-fetch STORE PUBLIC HTTPS_URL | video-rollback STORE PUBLIC")
}
