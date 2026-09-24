package main

import (
 "archive/zip"
 "bytes"
 "crypto/ed25519"
 "crypto/rand"
 "encoding/json"
 "image"
 "image/color"
 "image/png"
 "os"
 "path/filepath"
 "testing"
)
func videoFixture(t *testing.T, version string) (string, []byte, ed25519.PublicKey, ed25519.PrivateKey) {
 t.Helper();dir:=t.TempDir();for _,n:=range []string{"images","videos"}{if e:=os.Mkdir(filepath.Join(dir,n),0700);e!=nil{t.Fatal(e)}}
 im:=image.NewNRGBA(image.Rect(0,0,16,16));im.SetNRGBA(4,4,color.NRGBA{R:240,G:80,A:255});var b bytes.Buffer
 if e:=png.Encode(&b,im);e!=nil{t.Fatal(e)}
 if e:=os.WriteFile(filepath.Join(dir,"images/poster.png"),b.Bytes(),0600);e!=nil{t.Fatal(e)}
 // Installer preflight only; deliberately NOT a decodable H264 clip.
 mp4:=[]byte{0,0,0,24,'f','t','y','p','i','s','o','m',0,0,0,0,'i','s','o','m',0,0,0,0}
 if e:=os.WriteFile(filepath.Join(dir,"videos/clip.mp4"),mp4,0600);e!=nil{t.Fatal(e)}
 m:=VideoManifest{Schema:1,App:"standby-video",Version:version,IdleMS:1500,Loop:true,Poster:"poster",Assets:[]VideoAsset{
  {ID:"poster",File:"images/poster.png",License:"test",Kind:"image",Width:16,Height:16},
  {ID:"clip",File:"videos/clip.mp4",License:"test",Kind:"video",Width:320,Height:180,FPSNum:15,FPSDen:1},
 },Items:[]VideoItem{{Asset:"clip"}}}
 raw,e:=json.Marshal(m);if e!=nil{t.Fatal(e)};pub,priv,e:=ed25519.GenerateKey(rand.Reader);if e!=nil{t.Fatal(e)}
 return dir,raw,pub,priv
}
func TestVideoValidateSignedBoundary(t *testing.T) {
 dir,meta,pub,priv:=videoFixture(t,"one");raw,e:=PackVideo(dir,meta,priv);if e!=nil{t.Fatal(e)}
 v,e:=ValidateVideo(raw,pub);if e!=nil{t.Fatal(e)}
 if string(v.Index[:8])!="PUIVPL1\x00"||len(v.Payloads)!=2||len(v.Payloads[0])!=16*16*4{t.Fatal("index/pixels")}
 other,_,_:=ed25519.GenerateKey(rand.Reader);if _,e=ValidateVideo(raw,other);e==nil{t.Fatal("wrong signer admitted")}
 z,_:=zip.NewReader(bytes.NewReader(raw),int64(len(raw)));for _,f:=range z.File{if f.Name=="manifest.sig"{offset,e:=f.DataOffset();if e!=nil{t.Fatal(e)};bad:=append([]byte{},raw...);bad[offset]^=1;if _,e=ValidateVideo(bad,pub);e==nil{t.Fatal("bad signature admitted")}}}
 cases:=map[string]func(*VideoManifest){
  "external-url":func(m *VideoManifest){m.Assets[1].File="https://invalid/clip.mp4"},
  "traversal":func(m *VideoManifest){m.Assets[1].File="../clip.mp4"},
  "budget":func(m *VideoManifest){m.Assets[1].Width=1024},
  "fps":func(m *VideoManifest){m.Assets[1].FPSNum=60},
  "reference":func(m *VideoManifest){m.Items[0].Asset="missing"},
  "poster-video":func(m *VideoManifest){m.Poster="clip"},
  "app":func(m *VideoManifest){m.App="coffee-demo"},
 }
 for n,modify:=range cases{t.Run(n,func(t *testing.T){var x VideoManifest;_ = json.Unmarshal(meta,&x);modify(&x);b,_:=json.Marshal(x);if _,e:=PackVideo(dir,b,priv);e==nil{t.Fatal("invalid scene admitted")}})}
 unknown:=bytes.Replace(meta,[]byte(`"schema":1`),[]byte(`"schema":1,"exec":"sh"`),1)
 if _,e=PackVideo(dir,unknown,priv);e==nil{t.Fatal("executable field admitted")}
}
func TestVideoInstallRollbackAndPinnedGeneration(t *testing.T) {
 dir,meta,pub,priv:=videoFixture(t,"a");a,e:=PackVideo(dir,meta,priv);if e!=nil{t.Fatal(e)}
 bmeta:=bytes.Replace(meta,[]byte(`"version":"a"`),[]byte(`"version":"b"`),1);b,e:=PackVideo(dir,bmeta,priv);if e!=nil{t.Fatal(e)}
 cmeta:=bytes.Replace(meta,[]byte(`"version":"a"`),[]byte(`"version":"c"`),1);c,e:=PackVideo(dir,cmeta,priv);if e!=nil{t.Fatal(e)}
 store:=filepath.Join(t.TempDir(),"store");v,e:=InstallVideo(store,a,pub);if e!=nil{t.Fatal(e)}
 pinned,e:=os.Open(filepath.Join(store,v.ID,"asset-1"));if e!=nil{t.Fatal(e)};defer pinned.Close()
 if _,e=InstallVideo(store,b,pub);e!=nil{t.Fatal(e)};r,e:=RollbackVideo(store,pub);if e!=nil||r.ID!=v.ID{t.Fatalf("rollback: %v",e)}
 if _,e=InstallVideo(store,b,pub);e!=nil{t.Fatal(e)};if _,e=InstallVideo(store,c,pub);e!=nil{t.Fatal(e)}
 if _,e=os.Stat(filepath.Join(store,v.ID));!os.IsNotExist(e){t.Fatal("orphan generation retained")}
 data:=make([]byte,8);if _,e=pinned.ReadAt(data,0);e!=nil||string(data[4:8])!="ftyp"{t.Fatal("active generation not pinned")}
 before,_:=os.ReadFile(filepath.Join(store,"current"));if _,e=InstallVideo(store,[]byte("bad"),pub);e==nil{t.Fatal("bad bundle")};after,_:=os.ReadFile(filepath.Join(store,"current"));if !bytes.Equal(before,after){t.Fatal("bad bundle changed current")}
 mixed:=t.TempDir();if e=os.WriteFile(filepath.Join(mixed,"foreign"),[]byte("x"),0600);e!=nil{t.Fatal(e)};if _,e=InstallVideo(mixed,a,pub);e==nil{t.Fatal("mixed store")}
}
