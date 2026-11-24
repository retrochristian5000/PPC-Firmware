\ Manual regression for the OpenBIOS FCode reader.
\ Evaluate this file in an initialized OpenBIOS Forth environment.
\ It uses a counting byte reader, not a device register.

hex
0 value fcode-test-reads
: fcode-test-reader ( addr -- byte )
  drop
  fcode-test-reads 1+ to fcode-test-reads
  5a
;

create fcode-test-header
  8 c, 0 c, 0 c, 0 c, 0 c, 0 c, 9 c, 0 c,

: (test-fcode-num8) ( -- )
  0 to fcode-test-reads
  ['] fcode-test-reader to fcode-c@
  1000 to fcode-stream
  0 to fcode-bytes-read
  2 to fcode-bytes-left
  4 to fcode-spread
  fcode-num8 5a <> abort" FCode test: first byte"
  fcode-num8 5a <> abort" FCode test: second byte"
  fcode-stream 1008 <> abort" FCode test: spread"
  fcode-bytes-left 0 <> abort" FCode test: byte budget"
  ['] fcode-num8 catch 0= abort" FCode test: missing end check"
  fcode-test-reads 2 <> abort" FCode test: read beyond end"

  1 to fcode-bytes-left
  -1 to fcode-stream
  ['] fcode-num8 catch 0= abort" FCode test: missing overflow check"
  fcode-test-reads 2 <> abort" FCode test: read after overflow"

  ['] c@ to fcode-c@
  fcode-test-header to fcode-stream
  1 to fcode-spread
  1 to fcode-bytes-read
  -1 to fcode-bytes-left
  fcode-header
  fcode-bytes-left 1 <> abort" FCode test: header length"
  fcode-num8 0 <> abort" FCode test: end token"
  ['] fcode-num8 catch 0= abort" FCode test: header bound"

  fcode-push-state
  3 to fcode-bytes-read
  5 to fcode-bytes-left
  fcode-pop-state
  fcode-bytes-read 9 <> abort" FCode test: restored read count"
  fcode-bytes-left 0 <> abort" FCode test: restored byte budget"
;

: test-fcode-num8 ( -- )
  fcode-push-state
  ['] (test-fcode-num8) catch >r
  fcode-pop-state
  r> throw
  ." FCode reader regression passed" cr
;

test-fcode-num8
