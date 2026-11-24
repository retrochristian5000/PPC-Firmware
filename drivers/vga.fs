\
\ Fcode payload for QEMU VGA graphics card
\
\ This is the Forth source for an Fcode payload to initialise
\ the QEMU VGA graphics card.
\
\ (C) Copyright 2013 Mark Cave-Ayland
\

fcode-version3

\
\ Dictionary lookups for words that don't have an FCode
\

: (find-xt)   \ ( str len -- xt | -1 )
  $find if
    exit
  else
    -1
  then
;

" openbios-video-width" (find-xt) cell+ value openbios-video-width-xt
" openbios-video-height" (find-xt) cell+ value openbios-video-height-xt
" display-ih" (find-xt) cell+ value display-ih-xt
" depth-bits" (find-xt) cell+ value depth-bits-xt
" line-bytes" (find-xt) cell+ value line-bytes-xt

: openbios-video-width openbios-video-width-xt @ ;
: openbios-video-height openbios-video-height-xt @ ;
: display-ih display-ih-xt @ ;
: depth-bits depth-bits-xt @ ;
: line-bytes line-bytes-xt @ ;

" fb8-fillrect" (find-xt) value fb8-fillrect-xt
: fb8-fillrect fb8-fillrect-xt execute ;

" fw-cfg-read-file" (find-xt) value fw-cfg-read-file-xt
: fw-cfg-read-file fw-cfg-read-file-xt execute ;

\
\ IO port words
\

" ioc!" (find-xt) value ioc!-xt
" iow!" (find-xt) value iow!-xt

: ioc! ioc!-xt execute ;
: iow! iow!-xt execute ;

" le-w!" (find-xt) value le-w!-xt

: le-w! le-w!-xt execute ;

\
\ PCI
\

" pci-bar>pci-addr" (find-xt) value pci-bar>pci-addr-xt
: pci-bar>pci-addr pci-bar>pci-addr-xt execute ;

h# 10 constant cfg-bar0    \ Framebuffer BAR
h# 18 constant cfg-bar2    \ QEMU MMIO ioport BAR
-1 value fb-addr
-1 value mmio-addr

\
\ VGA registers
\

h# 3c0 constant vga-addr
h# 3c8 constant dac-write-addr
h# 3c9 constant dac-data-addr

defer vga-ioc!

: vga-legacy-ioc!  ( val addr )
  ioc!
;

: vga-mmio-ioc!  ( val addr )
  h# 3c0 - h# 400 + mmio-addr + c!
;

: vga-color!  ( r g b index -- )
  \ Set the VGA colour registers
  dac-write-addr vga-ioc! rot
  2 >> dac-data-addr vga-ioc! swap
  2 >> dac-data-addr vga-ioc!
  2 >> dac-data-addr vga-ioc!
;

\
\ VBE registers
\

h# 0 constant VBE_DISPI_INDEX_ID
h# 1 constant VBE_DISPI_INDEX_XRES
h# 2 constant VBE_DISPI_INDEX_YRES
h# 3 constant VBE_DISPI_INDEX_BPP
h# 4 constant VBE_DISPI_INDEX_ENABLE
h# 5 constant VBE_DISPI_INDEX_BANK
h# 6 constant VBE_DISPI_INDEX_VIRT_WIDTH
h# 7 constant VBE_DISPI_INDEX_VIRT_HEIGHT
h# 8 constant VBE_DISPI_INDEX_X_OFFSET
h# 9 constant VBE_DISPI_INDEX_Y_OFFSET
h# a constant VBE_DISPI_INDEX_NB

h# 0 constant VBE_DISPI_DISABLED
h# 1 constant VBE_DISPI_ENABLED

\
\ Bochs VBE register writes
\

defer vbe-iow!

: vbe-legacy-iow!  ( val addr -- )
  h# 1ce iow!
  h# 1d0 iow!
;

: vbe-mmio-iow!  ( val addr -- )
  1 lshift h# 500 + mmio-addr + le-w!
;

\
\ Initialise Bochs VBE mode
\

: vbe-init  ( -- )
  h# 0 vga-addr vga-ioc!    \ Enable blanking
  VBE_DISPI_DISABLED VBE_DISPI_INDEX_ENABLE vbe-iow!
  h# 0 VBE_DISPI_INDEX_X_OFFSET vbe-iow!
  h# 0 VBE_DISPI_INDEX_Y_OFFSET vbe-iow!
  openbios-video-width VBE_DISPI_INDEX_XRES vbe-iow!
  openbios-video-height VBE_DISPI_INDEX_YRES vbe-iow!
  depth-bits VBE_DISPI_INDEX_BPP vbe-iow!
  VBE_DISPI_ENABLED VBE_DISPI_INDEX_ENABLE vbe-iow!
  h# 0 vga-addr vga-ioc!
  h# 20 vga-addr vga-ioc!   \ Disable blanking
;

\
\ ATI Rage 128 Pro (1002:5046) native CRTC register support.
\ Do not use the unrelated Bochs VBE registers for this chipset.

" le-l!" (find-xt) value le-l!-xt
: le-l! le-l!-xt execute ;

\ active-package is a runtime OpenBIOS word, not an FCode token.
" active-package" (find-xt) value active-package-xt
: active-package active-package-xt execute ;

: rage128-device? ( -- flag )
  " vendor-id" active-package get-package-property if false exit then
  decode-int nip nip h# 1002 <> if false exit then
  " device-id" active-package get-package-property if false exit then
  decode-int nip nip h# 5046 =
;

: rage128-register! ( value offset -- )
  mmio-addr + le-l!
;

: rage128-pixel-format ( -- native-bits | 0 )
  depth-bits dup 8 = if drop h# 200 exit then
  dup 16 = if drop h# 400 exit then
  dup 32 = if drop h# 600 exit then
  drop 0
;

: rage128-hsync-delay ( -- bits )
  depth-bits dup 8 = if drop h# 12 exit then
  dup 16 = if drop h# 9 exit then
  drop h# 5
;

: rage128-mode-valid? ( -- flag )
  openbios-video-width dup d# 320 >= swap d# 2048 <= and
  openbios-video-width 7 and 0= and
  openbios-video-height dup d# 200 >= swap d# 2048 <= and
  rage128-pixel-format 0<> and
  line-bytes openbios-video-height * h# 1000000 <= and
;

: rage128-color! ( r g b index -- )
  h# b0 rage128-register!
  2 lshift swap d# 12 lshift or
  swap d# 22 lshift or
  h# b8 rage128-register!
;

: rage128-init-mode ( -- flag )
  rage128-mode-valid? 0= if false exit then

  \ Approximate VGA-style porch timing, with native Rage 128 geometry
  \ and 8-pixel CRTC units. QEMU does not yet emulate the full PLL/DDA.
  openbios-video-width d# 160 + 8 / 1-
  openbios-video-width 8 / 1- d# 16 lshift or
  h# 200 rage128-register!

  openbios-video-width 8 / 1- 2 + 3 lshift
  rage128-hsync-delay or
  d# 12 d# 16 lshift or h# 800000 or
  h# 204 rage128-register!

  openbios-video-height d# 45 + 1-
  openbios-video-height 1- d# 16 lshift or
  h# 208 rage128-register!

  openbios-video-height 1- d# 10 +
  d# 2 d# 16 lshift or h# 800000 or
  h# 20c rage128-register!

  openbios-video-width 8 / h# 22c rage128-register!
  0 h# 224 rage128-register!
  0 h# 228 rage128-register!

  \ PPC-endian framebuffer writes: ATI's 16/32-bit aperture swap.
  depth-bits dup 16 = if drop 1 else
    32 = if 2 else 0 then
  then
  h# e0 rage128-register!
  h# 100 h# 58 rage128-register!    \ enable 8-bit DAC components

  \ The CRTC is enabled last, after geometry and framebuffer setup.
  h# 3000000 rage128-pixel-format or
  h# 50 rage128-register!
  0 h# 54 rage128-register!
  true
;

\
\ PCI BAR mapping
\

: map-fb ( -- )
  cfg-bar0 pci-bar>pci-addr if   \ ( pci-addr.lo pci-addr.mid pci-addr.hi size )
    " map-in" $call-parent
    \ PCI map-in may return zero on failure; never use RAM address zero
    \ as a framebuffer or publish it in the display device tree.
    dup 0= if drop -1 then
    to fb-addr
  then
;

: map-mmio ( -- )
  cfg-bar2 pci-bar>pci-addr if   \ ( pci-addr.lo pci-addr.mid pci-addr.hi size )
    " map-in" $call-parent
    to mmio-addr
  then
;

\
\ QEMU VGA EDID
\
\ QEMU exposes the monitor EDID bytes at offset 0 in BAR2.  Publish the
\ base 128-byte EDID block in the Open Firmware device tree when the
\ standard EDID header is present.  encode-bytes copies the MMIO data into
\ firmware-owned property storage, so the property remains stable.
\

h# 80 constant edid-base-size

: qemu-edid-valid? ( -- flag )
  mmio-addr -1 = if
    false exit
  then

  mmio-addr       c@ h# 00 =
  mmio-addr 1 +   c@ h# ff = and
  mmio-addr 2 +   c@ h# ff = and
  mmio-addr 3 +   c@ h# ff = and
  mmio-addr 4 +   c@ h# ff = and
  mmio-addr 5 +   c@ h# ff = and
  mmio-addr 6 +   c@ h# ff = and
  mmio-addr 7 +   c@ h# 00 = and
;

: qemu-edid-install ( -- )
  qemu-edid-valid? if
    mmio-addr edid-base-size encode-bytes " EDID" property
  then
;

\
\ Legacy IO port or QEMU MMIO accesses
\
\ legacy: use standard VGA ioport registers
\ MMIO: use QEMU PCI MMIO VGA registers
\
\ If building for QEMU, default to MMIO access since it allows
\ programming of the VGA card regardless of its position in the
\ PCI topology
\

[IFDEF] CONFIG_QEMU
['] vga-mmio-ioc! to vga-ioc!
['] vbe-mmio-iow! to vbe-iow!
[ELSE]
['] vga-legacy-ioc! to vga-ioc!
['] vbe-legacy-iow! to vbe-iow!
[THEN]

\
\ Publically visible words
\

external

[IFDEF] CONFIG_MOL
defer mol-color!

\ Hook for MOL (see packages/molvideo.c)
\
\ Perhaps for neatness this there should be a separate molvga.fs
\ but let's leave it here for now.

: color!  ( r g b index -- )
  mol-color!
;

[ELSE]

\ Standard VGA

: color!  ( r g b index -- )
  rage128-device? if rage128-color! else vga-color! then
;

[THEN]

: fill-rectangle  ( color_ind x y width height -- )
  fb8-fillrect
;

: dimensions  ( -- width height )
  openbios-video-width
  openbios-video-height
;

: set-colors  ( table start count -- )
  0 do
    over dup        \ ( table start table table )
    c@ swap 1+      \ ( table start r table-g )
    dup c@ swap 1+  \ ( table start r g table-b )
    c@ 3 pick       \ ( table start r g b index )
    color!          \ ( table start )
    1+
    swap 3 + swap   \ ( table+3 start+1 )
  loop
;

\
\ Cancel Bochs VBE mode
\

: vbe-deinit ( -- )
  rage128-device? if exit then
  \ Switching VBE on and off clears the framebuffer
  VBE_DISPI_DISABLED VBE_DISPI_INDEX_ENABLE vbe-iow!
  VBE_DISPI_ENABLED VBE_DISPI_INDEX_ENABLE vbe-iow!
  VBE_DISPI_DISABLED VBE_DISPI_INDEX_ENABLE vbe-iow!
;

headerless

\
\ Installation
\

: qemu-vga-driver-install ( -- )
  rage128-device? if
    \ The ATI card needs real ATI registers, never Bochs VBE.
    mmio-addr -1 = if map-mmio then
    mmio-addr -1 = if
      ." Rage 128: BAR2 unavailable" cr exit
    then
    fb-addr -1 = if map-fb then
    fb-addr -1 = if
      ." Rage 128: BAR0 unavailable" cr exit
    then
    rage128-init-mode 0= if
      ." Rage 128: unsupported firmware video mode" cr exit
    then
  else
    mmio-addr -1 = if
      map-mmio

      \ The generic VGA model retains Bochs VBE and legacy fallback.
      mmio-addr -1 = if
        ['] vga-legacy-ioc! to vga-ioc!
        ['] vbe-legacy-iow! to vbe-iow!
      else
        ['] vga-mmio-ioc! to vga-ioc!
        ['] vbe-mmio-iow! to vbe-iow!
        qemu-edid-install
      then
      vbe-init
    then
  then

  \ Rage 128 maps BAR0 before programming its CRTC; generic VGA may
  \ map it here. Both paths must publish and install the framebuffer.
  fb-addr -1 = if map-fb then
  fb-addr -1 = if
    ." VGA: framebuffer BAR0 unavailable" cr exit
  then

  \ Every display exports its own mapped BAR0 address.
  fb-addr encode-int " address" property

  \ The framebuffer terminal in display.fs is global, not per head.
  \ Only the first opened display may take over /chosen/display.
  display-ih 0= if
    fb-addr to frame-buffer-adr
    default-font set-font

    openbios-video-width openbios-video-height
    over char-width / over char-height /
    fb8-install

    \ A zero-length marker identifies the Open Firmware boot
    \ display to Apple clients, not all installed PCI adapters.
    0 0 " AAPL,boot-display" property
  then
;

: qemu-vga-driver-init
  openbios-video-width encode-int " width" property
  openbios-video-height encode-int " height" property
  depth-bits encode-int " depth" property
  line-bytes encode-int " linebytes" property

  \ Never attach the generic QEMU VGA Mac NDRV to an ATI device.
  rage128-device? 0= if
  " /options" find-package drop s" vga-ndrv?" rot get-package-property not if
    decode-string 2swap 2drop    \ ( addr len )
    s" true" drop -rot comp 0= if
      \ Embed NDRV driver via fw-cfg if it exists
      " ndrv/qemu_vga.ndrv" fw-cfg-read-file if
        encode-string " driver,AAPL,MacOS,PowerPC" property
      then
    then
  then
  then

  ['] qemu-vga-driver-install is-install
;

qemu-vga-driver-init

end0
