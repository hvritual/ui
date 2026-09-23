package main

import (
	"image"
	"image/color"
)

// fitImage prepares pixels once, during installation/baking, never per UI frame.
// Minification integrates source-pixel area; enlargement uses bilinear samples.
// Accumulation uses premultiplied 16-bit channels, then unpremultiplies once.
// This prevents transparent padding from tinting edges black (or leaking hidden
// RGB). All weights are integer, giving the same result on native and ARM.
func fitImage(src image.Image, width, height int) *image.NRGBA {
	out := image.NewNRGBA(image.Rect(0, 0, width, height))
	sw, sh := src.Bounds().Dx(), src.Bounds().Dy()
	if sw < 1 || sh < 1 || width < 1 || height < 1 {
		return out
	}
	w, h := width, height
	if sw*height > sh*width {
		h = sh * width / sw
	} else {
		w = sw * height / sh
	}
	if w < 1 {
		w = 1
	}
	if h < 1 {
		h = 1
	}
	x0, y0 := (width-w)/2, (height-h)/2
	for y := 0; y < h; y++ {
		for x := 0; x < w; x++ {
			var r, g, b, a, weight uint64
			add := func(sx, sy int, n uint64) {
				cr, cg, cb, ca := src.At(src.Bounds().Min.X+sx, src.Bounds().Min.Y+sy).RGBA()
				r += uint64(cr) * n
				g += uint64(cg) * n
				b += uint64(cb) * n
				a += uint64(ca) * n
				weight += n
			}
			if w <= sw && h <= sh {
				// Footprints are expressed in units of 1/w and 1/h source pixels.
				left, right, top, bottom := x*sw, (x+1)*sw, y*sh, (y+1)*sh
				for sy := top / h; sy < (bottom+h-1)/h; sy++ {
					wy := min(bottom, (sy+1)*h) - max(top, sy*h)
					for sx := left / w; sx < (right+w-1)/w; sx++ {
						wx := min(right, (sx+1)*w) - max(left, sx*w)
						add(sx, sy, uint64(wx)*uint64(wy))
					}
				}
			} else {
				// Clamp before selecting neighbors so negative half-pixel positions
				// do not accidentally interpolate with pixel 1 at the outer edge.
				xq := max(0, min((2*x+1)*sw*128/w-128, (sw-1)*256))
				yq := max(0, min((2*y+1)*sh*128/h-128, (sh-1)*256))
				sx, sy, fx, fy := xq/256, yq/256, uint64(xq%256), uint64(yq%256)
				add(sx, sy, (256-fx)*(256-fy))
				add(min(sx+1, sw-1), sy, fx*(256-fy))
				add(sx, min(sy+1, sh-1), (256-fx)*fy)
				add(min(sx+1, sw-1), min(sy+1, sh-1), fx*fy)
			}
			c := color.NRGBA{}
			if a > 0 && weight > 0 {
				channel := func(n uint64) uint8 { return uint8(min(uint64(255), (n*255+a/2)/a)) }
				c = color.NRGBA{channel(r), channel(g), channel(b), uint8(min(uint64(255), (a*255+weight*65535/2)/(weight*65535)))}
				if c.A == 0 {
					c = color.NRGBA{}
				}
			}
			out.SetNRGBA(x+x0, y+y0, c)
		}
	}
	return out
}
