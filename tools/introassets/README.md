# introassets/

Source images for the boot intro's dragon logo animation (see
[src/i_intro.c](../../src/i_intro.c)). `dragon1.png`..`dragon4.png` are the
four line-art layers (head, body, tail, wordmark) that get rotated/scaled/
faded in to animate a dragon "jumping" onto screen.

These come from [lambertjamesd/n64brew2025](https://github.com/lambertjamesd/n64brew2025)
(`assets/images/intro/`), which in turn sourced them from the
N64brew-GameJam2024 repository - see the license header in `src/i_intro.c`
for the exact attribution. Both are MIT licensed.

Regenerate the `.sprite` files shipped in `src/filesystem/intro/` with:

```sh
mksprite -f I8 -o ../../src/filesystem/intro dragon1.png dragon2.png dragon3.png dragon4.png
```

(`I8` matches how i_intro.c shades them: the grayscale value is used as both
intensity and alpha, so black areas are transparent and the line art is
tinted via the RDP prim color instead of carrying its own color.)
