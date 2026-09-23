package main

import (
	"image"
	"image/color"
	"testing"
)

func TestResampleAreaCheckerboard(t *testing.T) {
	im := image.NewNRGBA(image.Rect(0, 0, 512, 256))
	for y := 0; y < 256; y++ {
		for x := 0; x < 512; x++ {
			v := uint8(0)
			if (x+y)%2 == 0 {
				v = 255
			}
			im.SetNRGBA(x, y, color.NRGBA{v, v, v, 255})
		}
	}
	out := fitImage(im, 256, 128)
	for y := 0; y < 128; y++ {
		for x := 0; x < 256; x++ {
			c := out.NRGBAAt(x, y)
			if c.R != 128 || c.G != 128 || c.B != 128 || c.A != 255 {
				t.Fatalf("aliased checkerboard at %d,%d: %v", x, y, c)
			}
		}
	}
}
func TestResampleTransparentEdge(t *testing.T) {
	im := image.NewNRGBA(image.Rect(0, 0, 2, 1))
	im.SetNRGBA(0, 0, color.NRGBA{255, 0, 0, 255})
	im.SetNRGBA(1, 0, color.NRGBA{0, 0, 255, 0}) // Hidden blue must not leak.
	out := fitImage(im, 1, 1)
	c := out.NRGBAAt(0, 0)
	if c != (color.NRGBA{255, 0, 0, 128}) {
		t.Fatal(c)
	}
	up := fitImage(im, 8, 4)
	c = up.NRGBAAt(3, 1)
	if c.R != 255 || c.G != 0 || c.B != 0 || c.A == 0 || c.A == 255 {
		t.Fatalf("bilinear straight-alpha edge: %v", c)
	}
}
func TestResampleIdentityAndFit(t *testing.T) {
	im := image.NewNRGBA(image.Rect(7, 9, 15, 13))
	for y := 9; y < 13; y++ {
		for x := 7; x < 15; x++ {
			im.SetNRGBA(x, y, color.NRGBA{uint8(x * 7), uint8(y * 9), 81, 255})
		}
	}
	out := fitImage(im, 8, 4)
	for y := 0; y < 4; y++ {
		for x := 0; x < 8; x++ {
			if out.NRGBAAt(x, y) != im.NRGBAAt(x+7, y+9) {
				t.Fatal("identity changed")
			}
		}
	}
	fitted := fitImage(im, 8, 8)
	if fitted.NRGBAAt(0, 0).A != 0 || fitted.NRGBAAt(0, 2).A != 255 || fitted.NRGBAAt(0, 6).A != 0 {
		t.Fatal("aspect fit padding")
	}
}
func TestFixtureCoverage(t *testing.T) {
	im := fixtureImage(0, 0)
	partial := 0
	// A cup ellipse edge is opaque inside, transparent outside: intermediate
	// coverage here proves AA, not merely the intentional low-alpha shadow.
	for y := 28; y < 60; y++ {
		for x := 76; x < 177; x++ {
			a := im.NRGBAAt(x, y).A
			if a > 0 && a < 255 {
				partial++
			}
		}
	}
	if partial < 20 {
		t.Fatalf("fixture contour is binary: partial=%d", partial)
	}
}
