\ Manual regression for FCode buffer capacity and initialization.
\ Requires an initialized OpenBIOS with dictionary-space available.
hex

: fcode-buffer-test-negative ( -- )
  -1 fcode-buffer-size drop
;

: fcode-buffer-test-too-large ( -- )
  dictionary-space fcode-buffer-size drop
;

: test-fcode-buffer-bounds ( -- )
  dictionary-space 0< abort" FCode test: missing dictionary capacity"
  #instance @ >r
  0 #instance !
  here >r
  ['] fcode-buffer-test-negative catch 0= abort" FCode test: negative accepted"
  ['] fcode-buffer-test-too-large catch 0= abort" FCode test: oversized accepted"
  here r> <> abort" FCode test: rejected buffer changed HERE"
  0 fcode-buffer-size 0 <> abort" FCode test: zero size"
  1 fcode-buffer-size cell <> abort" FCode test: buffer alignment"
  r> #instance !
;

: test-fcode-buffer-data ( -- )
  \ Create an unnamed dictionary entry through the actual defining word.
  0 0 header
  here >r
  5 b(buffer:)
  r> execute
  5 bounds ?do
    i c@ 0<> abort" FCode test: buffer not zeroed"
  loop
;

test-fcode-buffer-bounds
test-fcode-buffer-data
." FCode buffer regression passed" cr
