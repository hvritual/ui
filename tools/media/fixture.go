package main

import (
 "image"
 "image/color"
)

// fixtureImage draws original coffee cup illustrations for diagnostic bundles.
// These are clearly marked test assets, not photographs of the user's products.
func fixtureImage(drink, variant int) *image.NRGBA {
 im := image.NewNRGBA(image.Rect(0,0,Width,Height))
 ellipse := func(cx,cy,rx,ry int,c color.NRGBA) {
  for y:=cy-ry;y<=cy+ry;y++ {for x:=cx-rx;x<=cx+rx;x++ {
   dx,dy:=x-cx,y-cy
   if dx*dx*ry*ry+dy*dy*rx*rx<=rx*rx*ry*ry {im.SetNRGBA(x,y,c)}
  }}
 }
 cream:=color.NRGBA{243,238,224,255}; shade:=color.NRGBA{207,200,181,255}
 if variant==1 {cream=color.NRGBA{184,215,211,255};shade=color.NRGBA{111,157,154,255}}
 coffee:=color.NRGBA{87,45,22,255}
 if drink==1 {coffee=color.NRGBA{49,31,22,255}}
 if drink==2||drink==3||drink==4 {coffee=color.NRGBA{176,120,69,255}}
 if drink==5 {coffee=color.NRGBA{105,69,46,255}}
 if drink==6 {coffee=color.NRGBA{181,108,31,255}}
 if drink==7 {coffee=color.NRGBA{194,222,223,255}}
 ellipse(129,115,62,6,color.NRGBA{50,53,44,18})
 ellipse(128,109,65,8,shade)
 ellipse(126,107,63,7,cream)
 ellipse(126,107,45,3,color.NRGBA{167,157,135,60})
 ellipse(179,71,22,20,shade)
 ellipse(177,69,22,18,cream)
 ellipse(180,70,12,10,color.NRGBA{})
 for y:=43;y<=94;y++ {
  half:=45-(y-43)*9/51
  for x:=127-half;x<=127+half;x++ {
   c:=cream
   if x>127+half-7 {c=shade}
   if drink==6||drink==7 {c=color.NRGBA{216,227,218,235}}
   im.SetNRGBA(x,y,c)
  }
 }
 ellipse(127,94,36,10,shade)
 ellipse(124,91,34,10,cream)
 ellipse(126,45,46,14,shade)
 ellipse(126,43,45,13,cream)
 ellipse(126,43,39,10,coffee)
 if drink==2||drink==3||drink==4 {
  for i:=0;i<4;i++ {ellipse(125,40+i*2,12-i*2,2,color.NRGBA{250,240,207,255})}
 }
 if drink==5 {ellipse(124,42,12,4,color.NRGBA{208,167,116,255})}
 for k:=0;k<3;k++ {for y:=8;y<27;y++ {x:=109+k*17+(y%10-5)/3;im.SetNRGBA(x,y,color.NRGBA{126,139,130,80});im.SetNRGBA(x+1,y,color.NRGBA{126,139,130,50})}}
 if drink<6 {ellipse(66,99,8,5,color.NRGBA{101,68,45,255});ellipse(57,107,7,4,color.NRGBA{118,79,48,255})}
 return im
}
