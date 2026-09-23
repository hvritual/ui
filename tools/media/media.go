// Package main implements signed, data-only image replacement for the UI.
// It never mounts USB drives, executes bundle content or modifies UI code.
package main

import (
 "archive/zip"
 "bytes"
 "crypto/ed25519"
 "crypto/rand"
 "crypto/sha256"
 "encoding/binary"
 "encoding/hex"
 "encoding/json"
 "errors"
 "fmt"
 "hash/crc32"
 "image"
 _ "image/jpeg"
 "image/png"
 "io"
 "net/http"
 "net/url"
 "os"
 "path/filepath"
 "regexp"
 "runtime/debug"
 "sort"
 "strings"
 "time"
)

const MaxBundle = 16 << 20
const MaxImage = 2 << 20
// Both axes must be powers of two in the pinned PocketJS texture ABI.
const Width, Height = 256, 128
var Slots = []string{"espresso", "americano", "latte", "cappuccino", "flatwhite", "mocha", "tea", "water"}
var hexID = regexp.MustCompile(`^[a-f0-9]{64}$`)
var versionID = regexp.MustCompile(`^[a-zA-Z0-9][a-zA-Z0-9._-]{0,47}$`)
type Entry struct { ID string `json:"id"`; File string `json:"file"`; SHA256 string `json:"sha256"`; License string `json:"license"` }
type Manifest struct { Schema int `json:"schema"`; App string `json:"app"`; Version string `json:"version"`; Images []Entry `json:"images"` }
type Validated struct { Manifest Manifest; Packet []byte; ID string }
func hash(b []byte) string { sum:=sha256.Sum256(b); return hex.EncodeToString(sum[:]) }
func limited(r io.Reader,n int64)([]byte,error){ b,e:=io.ReadAll(io.LimitReader(r,n+1));if e==nil&&int64(len(b))>n {e=errors.New("size budget exceeded")};return b,e }
func regular(path string,max int64)([]byte,error){
 st,e:=os.Lstat(path);if e!=nil{return nil,e};if !st.Mode().IsRegular()||st.Size()>max{return nil,errors.New("not a bounded regular file")}
 f,e:=os.Open(path);if e!=nil{return nil,e};defer f.Close();now,e:=f.Stat();if e!=nil{return nil,e};if !os.SameFile(st,now){return nil,errors.New("source changed while opening")};return limited(f,max)
}
// Reject duplicate keys rather than letting later values silently replace earlier ones.
func uniqueJSON(raw []byte)error{
 d:=json.NewDecoder(bytes.NewReader(raw));var walk func()error
 walk=func()error{t,e:=d.Token();if e!=nil{return e};switch t{
 case json.Delim('{'):seen:=map[string]bool{};for d.More(){k,e:=d.Token();if e!=nil{return e};s,ok:=k.(string);if !ok||seen[s]{return errors.New("duplicate JSON key")};seen[s]=true;if e=walk();e!=nil{return e}};_,e=d.Token();return e
 case json.Delim('['):for d.More(){if e=walk();e!=nil{return e}};_,e=d.Token();return e
 };return nil}
 if e:=walk();e!=nil{return e};if _,e:=d.Token();e!=io.EOF{return errors.New("trailing JSON")};return nil
}
func parseManifest(raw []byte)(Manifest,error){
 var m Manifest;if e:=uniqueJSON(raw);e!=nil{return m,e};d:=json.NewDecoder(bytes.NewReader(raw));d.DisallowUnknownFields();if e:=d.Decode(&m);e!=nil{return m,e}
 if m.Schema!=1||m.App!="coffee-demo"||!versionID.MatchString(m.Version)||len(m.Images)!=len(Slots){return m,errors.New("manifest schema/app/version/slots rejected")}
 for i,e:=range m.Images{if e.ID!=Slots[i]||!hexID.MatchString(e.SHA256)||e.License==""||len(e.License)>256||(e.File!="images/"+e.ID+".png"&&e.File!="images/"+e.ID+".jpg"){return m,errors.New("image identity/path/license rejected")}}
 return m,nil
}
func Validate(raw []byte,key ed25519.PublicKey)(*Validated,error){
 if len(raw)>MaxBundle||len(key)!=ed25519.PublicKeySize{return nil,errors.New("bundle/key budget")}
 z,e:=zip.NewReader(bytes.NewReader(raw),int64(len(raw)));if e!=nil{return nil,e};if len(z.File)!=len(Slots)+2{return nil,errors.New("unexpected archive entries")}
 files:=map[string]*zip.File{};for _,f:=range z.File{if files[f.Name]!=nil||!f.Mode().IsRegular()||f.UncompressedSize64>MaxImage||f.Flags&1!=0{return nil,errors.New("archive path/type/size rejected")};files[f.Name]=f}
 read:=func(name string,max int64)([]byte,error){f:=files[name];if f==nil{return nil,errors.New("archive file missing")};r,e:=f.Open();if e!=nil{return nil,e};defer r.Close();return limited(r,max)}
 manifest,e:=read("manifest.json",16<<10);if e!=nil{return nil,e};sig,e:=read("manifest.sig",ed25519.SignatureSize);if e!=nil{return nil,e}
 if !ed25519.Verify(key,manifest,sig){return nil,errors.New("signature verification failed")};m,e:=parseManifest(manifest);if e!=nil{return nil,e}
 pixels:=make([]byte,len(Slots)*Width*Height*4)
 for i,entry:=range m.Images{
  encoded,e:=read(entry.File,MaxImage);if e!=nil{return nil,e};if hash(encoded)!=entry.SHA256{return nil,errors.New("image digest mismatch")}
  cfg,format,e:=image.DecodeConfig(bytes.NewReader(encoded));if e!=nil{return nil,e}
  if (format!="png"&&format!="jpeg")||cfg.Width<1||cfg.Height<1||cfg.Width>2048||cfg.Height>2048||cfg.Width*cfg.Height>1<<20{return nil,errors.New("unsupported image or decoded pixel budget")}
  if (format=="png")!=strings.HasSuffix(entry.File,".png"){return nil,errors.New("format/extension mismatch")}
  img,_,e:=image.Decode(bytes.NewReader(encoded));if e!=nil{return nil,e}
  // Prepare target-size antialiased, straight-alpha pixels outside the UI.
  resized := fitImage(img, Width, Height)
  copy(pixels[i*Width*Height*4:(i+1)*Width*Height*4], resized.Pix)
 }
 header:=make([]byte,64);copy(header,[]byte("PUIIMG1\x00"));fields:=[]uint32{1,uint32(len(Slots)),Width,Height,uint32(len(pixels)),crc32.ChecksumIEEE(pixels)};for i,v:=range fields{binary.LittleEndian.PutUint32(header[8+i*4:],v)};mh:=sha256.Sum256(manifest);copy(header[32:],mh[:]);return &Validated{m,append(header,pixels...),hash(raw)},nil
}
// TLS trust is not bypassed. The configured URL is never logged with credentials.
func download(address string,client *http.Client)([]byte,error){
 u,e:=url.Parse(address);if e!=nil||u.Scheme!="https"||u.Hostname()==""||u.User!=nil||u.Fragment!=""{return nil,errors.New("HTTPS URL without credentials required")}
 req,e:=http.NewRequest("GET",address,nil);if e!=nil{return nil,e};req.Header.Set("Accept-Encoding","identity");resp,e:=client.Do(req);if e!=nil{return nil,errors.New("HTTPS transfer failed")};defer resp.Body.Close()
 if resp.StatusCode!=200||resp.ContentLength>MaxBundle||(resp.Header.Get("Content-Encoding")!=""&&resp.Header.Get("Content-Encoding")!="identity"){return nil,errors.New("HTTPS status/encoding/size rejected")};return limited(resp.Body,MaxBundle)
}
func HTTPSClient()*http.Client{return &http.Client{Timeout:90*time.Second,Transport:&http.Transport{Proxy:http.ProxyFromEnvironment,TLSHandshakeTimeout:10*time.Second,ResponseHeaderTimeout:15*time.Second,DisableCompression:true},CheckRedirect:func(_ *http.Request,_ []*http.Request)error{return errors.New("redirects disabled")}}}
func syncDir(dir string)error{f,e:=os.Open(dir);if e!=nil{return e};defer f.Close();return f.Sync()}
func ensureStore(dir string)error{if e:=os.MkdirAll(dir,0700);e!=nil{return e};st,e:=os.Lstat(dir);if e!=nil{return e};if !st.IsDir()||st.Mode()&os.ModeSymlink!=0||st.Mode().Perm()&0022!=0{return errors.New("store must be private non-symlink directory")};return nil}
func atomicWrite(dir,name string,b []byte)error{
 f,e:=os.CreateTemp(dir,".pending-");if e!=nil{return e};tmp:=f.Name();defer os.Remove(tmp);if _,e=f.Write(b);e==nil{e=f.Sync()};ce:=f.Close();if e!=nil{return e};if ce!=nil{return ce};if e=os.Rename(tmp,filepath.Join(dir,name));e!=nil{return e};return syncDir(dir)
}
func state(dir string)(string,string,error){
 b,e:=regular(filepath.Join(dir,"current"),130);if os.IsNotExist(e){return "","",nil};if e!=nil{return "","",e}
 v:=strings.Split(strings.TrimSuffix(string(b),"\n"),"\n");if len(v)!=2||!hexID.MatchString(v[0])||(v[1]!="-"&&!hexID.MatchString(v[1])){return "","",errors.New("corrupt activation state")};return v[0],v[1],nil
}
func activate(dir string,v *Validated,raw []byte)error{
 current,_,e:=state(dir);if e!=nil{return e}
 // Originals are retained for authenticated rollback. Store and parents are trusted local state.
 if e=atomicWrite(dir,v.ID+".bundle",raw);e!=nil{return e};if e=atomicWrite(dir,v.ID+".rgba",v.Packet);e!=nil{return e};receipt,_:=json.Marshal(v.Manifest);if e=atomicWrite(dir,v.ID+".json",receipt);e!=nil{return e}
 if current==v.ID{return nil};if current==""{current="-"};if e=atomicWrite(dir,"current",[]byte(v.ID+"\n"+current+"\n"));e!=nil{return e}
 entries,e:=os.ReadDir(dir);if e!=nil{return e};for _,f:=range entries{stem:=strings.TrimSuffix(f.Name(),filepath.Ext(f.Name()));ext:=filepath.Ext(f.Name());if hexID.MatchString(stem)&&stem!=v.ID&&stem!=current&&(ext==".rgba"||ext==".bundle"||ext==".json"){if e=os.Remove(filepath.Join(dir,f.Name()));e!=nil{return e}}};return syncDir(dir)
}
func Install(dir string,raw []byte,key ed25519.PublicKey)(*Validated,error){v,e:=Validate(raw,key);if e!=nil{return nil,e};if e=ensureStore(dir);e!=nil{return nil,e};unlock,e:=storeLock(dir);if e!=nil{return nil,e};defer unlock();if e=activate(dir,v,raw);e!=nil{return nil,e};return v,nil}
func Rollback(dir string,key ed25519.PublicKey)(*Validated,error){
 if e:=ensureStore(dir);e!=nil{return nil,e};unlock,e:=storeLock(dir);if e!=nil{return nil,e};defer unlock();_,prev,e:=state(dir);if e!=nil{return nil,e};if prev==""||prev=="-"{return nil,errors.New("no rollback generation")}
 b,e:=regular(filepath.Join(dir,prev+".bundle"),MaxBundle);if e!=nil{return nil,e};v,e:=Validate(b,key);if e!=nil{return nil,e};if v.ID!=prev{return nil,errors.New("rollback digest mismatch")};if e=activate(dir,v,b);e!=nil{return nil,e};return v,nil
}
func Pack(dir,version string,key ed25519.PrivateKey)([]byte,error){
 m:=Manifest{Schema:1,App:"coffee-demo",Version:version};contents:=map[string][]byte{}
 for _,id:=range Slots{filename:=id+".png";b,e:=regular(filepath.Join(dir,filename),MaxImage);if os.IsNotExist(e){filename=id+".jpg";b,e=regular(filepath.Join(dir,filename),MaxImage)};if e!=nil{return nil,e};name:="images/"+filename;m.Images=append(m.Images,Entry{id,name,hash(b),"operator-supplied; authorization required"});contents[name]=b}
 raw,e:=json.Marshal(m);if e!=nil{return nil,e};contents["manifest.json"]=raw;contents["manifest.sig"]=ed25519.Sign(key,raw)
 var out bytes.Buffer;z:=zip.NewWriter(&out);names:=[]string{};for n:=range contents{names=append(names,n)};sort.Strings(names)
 for _,n:=range names{h:=&zip.FileHeader{Name:n,Method:zip.Deflate};h.SetMode(0600);w,e:=z.CreateHeader(h);if e!=nil{return nil,e};if _,e=w.Write(contents[n]);e!=nil{return nil,e}}
 if e=z.Close();e!=nil{return nil,e};if _,e=Validate(out.Bytes(),key.Public().(ed25519.PublicKey));e!=nil{return nil,e};return out.Bytes(),nil
}
func publicKey(path string)(ed25519.PublicKey,error){b,e:=regular(path,256);if e!=nil{return nil,e};k,e:=hex.DecodeString(strings.TrimSpace(string(b)));if e!=nil||len(k)!=32{return nil,errors.New("invalid public key")};return ed25519.PublicKey(k),nil}
func privateKey(path string)(ed25519.PrivateKey,error){b,e:=regular(path,256);if e!=nil{return nil,e};k,e:=hex.DecodeString(strings.TrimSpace(string(b)));if e!=nil||len(k)!=ed25519.SeedSize{return nil,errors.New("invalid private seed")};return ed25519.NewKeyFromSeed(k),nil}
func writeNew(path string,b []byte)error{f,e:=os.OpenFile(path,os.O_WRONLY|os.O_CREATE|os.O_EXCL,0600);if e!=nil{return e};_,we:=f.Write(b);ce:=f.Close();if we!=nil{return we};return ce}
func fixture(dir string,variant int)error{
 if e:=os.MkdirAll(dir,0700);e!=nil{return e}
 for i,id:=range Slots {var b bytes.Buffer;if e:=png.Encode(&b,fixtureImage(i,variant));e!=nil{return e};if e:=os.WriteFile(filepath.Join(dir,id+".png"),b.Bytes(),0600);e!=nil{return e}}
 return nil
}
func keygen(prefix string)error{pub,priv,e:=ed25519.GenerateKey(rand.Reader);if e!=nil{return e};if e=writeNew(prefix+".private",[]byte(hex.EncodeToString(priv.Seed())+"\n"));e!=nil{return e};return writeNew(prefix+".public",[]byte(hex.EncodeToString(pub)+"\n"))}
func main(){debug.SetMemoryLimit(64<<20);if e:=command(os.Args[1:]);e!=nil{fmt.Fprintln(os.Stderr,"MEDIA_ERROR:",e);os.Exit(1)}}
func command(a []string)error{
 if len(a)==0{return errors.New("usage: mediactl keygen PREFIX | pack IMAGE_DIR VERSION PRIVATE_KEY OUTPUT | install STORE PUBLIC_KEY FILE | fetch STORE PUBLIC_KEY HTTPS_URL | rollback STORE PUBLIC_KEY | status STORE")}
 switch a[0]{
 case "keygen":if len(a)!=2{break};return keygen(a[1])
 case "fixture":if len(a)!=3{break};v:=0;if a[2]=="b"{v=1};return fixture(a[1],v)
 case "pack":if len(a)!=5{break};key,e:=privateKey(a[3]);if e!=nil{return e};b,e:=Pack(a[1],a[2],key);if e!=nil{return e};return writeNew(a[4],b)
 case "install","fetch":if len(a)!=4{break};key,e:=publicKey(a[2]);if e!=nil{return e};var b []byte;if a[0]=="fetch"{b,e=download(a[3],HTTPSClient())}else{b,e=regular(a[3],MaxBundle)};if e!=nil{return e};v,e:=Install(a[1],b,key);if e!=nil{return e};fmt.Printf("MEDIA_INSTALLED version=%s requested_generation=%s ui_application=deferred-until-safe-point\n",v.Manifest.Version,v.ID);return nil
 case "rollback":if len(a)!=3{break};key,e:=publicKey(a[2]);if e!=nil{return e};v,e:=Rollback(a[1],key);if e!=nil{return e};fmt.Printf("MEDIA_ROLLBACK requested_generation=%s\n",v.ID);return nil
 case "status":if len(a)!=2{break};c,p,e:=state(a[1]);if e!=nil{return e};b,_:=json.Marshal(map[string]string{"requested":c,"previous":p,"note":"requested is not proof of on-screen application; inspect UI log"});fmt.Println(string(b));return nil
 };return errors.New("invalid command arguments")
}
