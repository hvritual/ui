package main

import (
 "archive/zip"
 "bytes"
 "crypto/ed25519"
 "crypto/rand"
 "encoding/binary"
 "encoding/hex"
 "encoding/json"
 "hash/crc32"
 "image"
 "image/color"
 "image/jpeg"
 "image/png"
 "io"
 "net/http"
 "net/http/httptest"
 "os"
 "path/filepath"
 "testing"
)
func testKey(t *testing.T)(ed25519.PublicKey,ed25519.PrivateKey){t.Helper();p,s,e:=ed25519.GenerateKey(rand.Reader);if e!=nil{t.Fatal(e)};return p,s}
func testPack(t *testing.T,ver string,key ed25519.PrivateKey)[]byte{t.Helper();d:=t.TempDir();if e:=fixture(d,0);e!=nil{t.Fatal(e)};p,e:=Pack(d,ver,key);if e!=nil{t.Fatal(e)};return p}
func rewrite(t *testing.T,raw []byte,fn func(map[string][]byte))[]byte{t.Helper();z,e:=zip.NewReader(bytes.NewReader(raw),int64(len(raw)));if e!=nil{t.Fatal(e)};m:=map[string][]byte{};for _,f:=range z.File{r,_:=f.Open();m[f.Name],_=io.ReadAll(r);r.Close()};fn(m);var out bytes.Buffer;w:=zip.NewWriter(&out);for n,b:=range m{f,_:=w.Create(n);f.Write(b)};w.Close();return out.Bytes()}
func TestValidateAndPacket(t *testing.T){p,s:=testKey(t);raw:=testPack(t,"release-a",s);v,e:=Validate(raw,p);if e!=nil{t.Fatal(e)};if len(v.Packet)!=64+8*Width*Height*4{t.Fatal("packet size")};if binary.LittleEndian.Uint32(v.Packet[28:])!=crc32.ChecksumIEEE(v.Packet[64:]){t.Fatal("crc")};if v.ID!=hash(raw){t.Fatal("identity")}}
func TestExternalPackagesFailClosed(t *testing.T){
 p,s:=testKey(t);raw:=testPack(t,"release-a",s);other,_:=testKey(t)
 tests:=[]struct{name string;data []byte;key ed25519.PublicKey}{
 {"wrong-key",raw,other},{"truncated",raw[:len(raw)/2],p},{"corrupt",[]byte("invalid"),p},
 {"image-tamper",rewrite(t,raw,func(m map[string][]byte){m["images/latte.png"]=[]byte("changed")}),p},
 {"signature-tamper",rewrite(t,raw,func(m map[string][]byte){m["manifest.sig"][0]^=1}),p},
 {"code-entry",rewrite(t,raw,func(m map[string][]byte){m["app.js"]=[]byte("run()")}),p},
 {"traversal",rewrite(t,raw,func(m map[string][]byte){m["../outside"]=m["images/latte.png"];delete(m,"images/latte.png")}),p},
 {"oversized",make([]byte,MaxBundle+1),p},
 }
 for _,tt:=range tests{t.Run(tt.name,func(t *testing.T){if _,e:=Validate(tt.data,tt.key);e==nil{t.Fatal("accepted unsafe bundle")}})}
}
func TestSignedInvalidManifestRejected(t *testing.T){
 p,s:=testKey(t);raw:=testPack(t,"ok",s)
 for _,kind:=range []string{"duplicate-key","unknown-field","bad-app","bad-slot","trailing-json","missing-license","bad-format"}{t.Run(kind,func(t *testing.T){bad:=rewrite(t,raw,func(m map[string][]byte){var v map[string]any;json.Unmarshal(m["manifest.json"],&v);switch kind{
 case "unknown-field":v["script"]="app.js"
 case "bad-app":v["app"]="other"
 case "bad-slot":v["images"].([]any)[0].(map[string]any)["id"]="other"
 case "missing-license":v["images"].([]any)[0].(map[string]any)["license"]=""
 case "bad-format":v["images"].([]any)[0].(map[string]any)["file"]="images/espresso.webp"
 };b,_:=json.Marshal(v);if kind=="duplicate-key"{b=append([]byte(`{"schema":2,`),b[1:]...)};if kind=="trailing-json"{b=append(b,[]byte("{}")...)};m["manifest.json"]=b;m["manifest.sig"]=ed25519.Sign(s,b)});if _,e:=Validate(bad,p);e==nil{t.Fatal("accepted")}})}
}
func TestImagesPNGJPEGAndBudget(t *testing.T){
 _,s:=testKey(t);d:=t.TempDir();fixture(d,0);im:=image.NewNRGBA(image.Rect(0,0,32,64));im.Set(0,0,color.NRGBA{20,30,40,255});var b bytes.Buffer;jpeg.Encode(&b,im,nil);os.Remove(filepath.Join(d,"latte.png"));os.WriteFile(filepath.Join(d,"latte.jpg"),b.Bytes(),0600)
 if _,e:=Pack(d,"jpeg-test",s);e!=nil{t.Fatal(e)};giant:=image.NewNRGBA(image.Rect(0,0,2049,1));b.Reset();png.Encode(&b,giant);os.WriteFile(filepath.Join(d,"espresso.png"),b.Bytes(),0600);if _,e:=Pack(d,"giant",s);e==nil{t.Fatal("oversized image accepted")}
}
func TestInstallRollbackAndRetention(t *testing.T){
 p,s:=testKey(t);dir:=filepath.Join(t.TempDir(),"store");a:=testPack(t,"a",s);b:=testPack(t,"b",s);c:=testPack(t,"c",s)
 va,e:=Install(dir,a,p);if e!=nil{t.Fatal(e)};vb,e:=Install(dir,b,p);if e!=nil{t.Fatal(e)};cur,prev,e:=state(dir);if e!=nil||cur!=vb.ID||prev!=va.ID{t.Fatal("activation pair")}
 if _,e=Install(dir,[]byte("bad"),p);e==nil{t.Fatal("bad installed")};cur2,prev2,_:=state(dir);if cur2!=cur||prev2!=prev{t.Fatal("failure changed current")}
 r,e:=Rollback(dir,p);if e!=nil||r.ID!=va.ID{t.Fatal("rollback",e)};vc,e:=Install(dir,c,p);if e!=nil{t.Fatal(e)};cur,prev,_=state(dir);if cur!=vc.ID||prev!=va.ID{t.Fatal("rollback pair")}
 if _,e=os.Stat(filepath.Join(dir,vb.ID+".bundle"));!os.IsNotExist(e){t.Fatal("old generation not evicted")};if _,e=Install(dir,c,p);e!=nil{t.Fatal("idempotent",e)};_,prev,_=state(dir);if prev!=va.ID{t.Fatal("idempotent loses fallback")}
}
func TestRollbackReverifiesOriginal(t *testing.T){p,s:=testKey(t);dir:=filepath.Join(t.TempDir(),"store");a:=testPack(t,"a",s);b:=testPack(t,"b",s);va,_:=Install(dir,a,p);Install(dir,b,p);os.WriteFile(filepath.Join(dir,va.ID+".bundle"),[]byte("bad"),0600);if _,e:=Rollback(dir,p);e==nil{t.Fatal("corrupt rollback accepted")};cur,_,_:=state(dir);if cur!=hash(b){t.Fatal("changed activation")}}
func TestStoreSafety(t *testing.T){p,s:=testKey(t);raw:=testPack(t,"ok",s);base:=t.TempDir();dir:=filepath.Join(base,"dir");os.Mkdir(dir,0700);os.Symlink(dir,filepath.Join(base,"link"));if _,e:=Install(filepath.Join(base,"link"),raw,p);e==nil{t.Fatal("symlink store")};os.Chmod(dir,0777);if _,e:=Install(dir,raw,p);e==nil{t.Fatal("writable store")};os.Chmod(dir,0700);os.WriteFile(filepath.Join(dir,"current"),[]byte("../../"),0600);if _,e:=Install(dir,raw,p);e==nil{t.Fatal("corrupt current")}}
func TestSourceSymlinkAndKeyPermissions(t *testing.T){d:=t.TempDir();f:=filepath.Join(d,"a");os.WriteFile(f,[]byte("data"),0600);os.Symlink(f,f+"-link");if _,e:=regular(f+"-link",20);e==nil{t.Fatal("followed symlink")};if _,e:=regular(d,20);e==nil{t.Fatal("accepted directory")};if e:=keygen(filepath.Join(d,"key"));e!=nil{t.Fatal(e)};if e:=keygen(filepath.Join(d,"key"));e==nil{t.Fatal("overwrote key")};st,_:=os.Stat(filepath.Join(d,"key.private"));if st.Mode().Perm()!=0600{t.Fatal("private permissions")}}
func TestHTTPS(t *testing.T){
 p,s:=testKey(t);raw:=testPack(t,"network",s);srv:=httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter,r *http.Request){if r.URL.Path=="/redirect"{http.Redirect(w,r,"/ok",302);return};if r.URL.Path=="/partial"{w.Header().Set("Content-Length",fmtInt(len(raw)+10));w.Write(raw[:10]);return};w.Write(raw)}));defer srv.Close()
 client:=srv.Client();client.CheckRedirect=HTTPSClient().CheckRedirect;client.Timeout=HTTPSClient().Timeout;got,e:=download(srv.URL+"/ok",client);if e!=nil||!bytes.Equal(got,raw){t.Fatal("TLS transfer",e)};if _,e=Validate(got,p);e!=nil{t.Fatal(e)}
 if _,e=download(srv.URL+"/ok",HTTPSClient());e==nil{t.Fatal("untrusted TLS accepted")};for _,address:=range []string{"http://localhost/bundle","https://user:password@localhost/bundle",srv.URL+"/redirect",srv.URL+"/partial"}{if _,e=download(address,client);e==nil{t.Fatal("unsafe transfer accepted",address)}}
}
func fmtInt(n int)string{b,_:=json.Marshal(n);return string(b)}
func TestDuplicateArchiveNames(t *testing.T){p,s:=testKey(t);raw:=testPack(t,"ok",s);z,_:=zip.NewReader(bytes.NewReader(raw),int64(len(raw)));var b bytes.Buffer;w:=zip.NewWriter(&b);for i,f:=range z.File{name:=f.Name;if i==1{name=z.File[0].Name};x,_:=w.Create(name);r,_:=f.Open();io.Copy(x,r);r.Close()};w.Close();if _,e:=Validate(b.Bytes(),p);e==nil{t.Fatal("duplicate accepted")}}
func TestPublicKeyNotBundleControlled(t *testing.T){p,s:=testKey(t);_,bad:=testKey(t);raw:=testPack(t,"attacker",bad);if _,e:=Validate(raw,p);e==nil{t.Fatal("wrong trust root")};f:=filepath.Join(t.TempDir(),"pub");os.WriteFile(f,[]byte(hex.EncodeToString(s.Public().(ed25519.PublicKey))),0600);v,e:=publicKey(f);if e!=nil||!bytes.Equal(v,p){t.Fatal("pub parsing")}}
