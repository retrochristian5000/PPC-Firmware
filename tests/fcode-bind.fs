\ Manual regression: evaluate in initialized OpenBIOS Forth.
hex

create fcode-bind-test-first 8 c, 0 c,
create fcode-bind-test-last  f c, ff c,
create fcode-bind-test-system 0 c,
create fcode-bind-test-vendor 7 c, ff c,

: fcode-bind-test-stream ( addr -- )
  to fcode-stream
  1 to fcode-spread
  0 to fcode-bytes-read
  2 to fcode-bytes-left
  ['] c@ to fcode-c@
;

: fcode-bind-test-bad-index ( -- )
  1000 fcode-ptr drop
;

: fcode-bind-test-negative-index ( -- )
  -1 fcode-ptr drop
;

: (test-fcode-bind) ( -- )
  fcode-bind-test-first fcode-bind-test-stream
  here >r
  fcode!
  800 fcode>xt r> <> abort" FCode test: first local binding"

  fcode-bind-test-last fcode-bind-test-stream
  here >r
  fcode!
  fff fcode>xt r> <> abort" FCode test: last local binding"

  0 fcode>xt >r
  fcode-bind-test-system fcode-bind-test-stream
  ['] fcode! catch 0= abort" FCode test: accepted system definition"
  0 fcode>xt r> <> abort" FCode test: changed system token"

  7ff fcode>xt >r
  fcode-bind-test-vendor fcode-bind-test-stream
  ['] fcode! catch 0= abort" FCode test: accepted vendor definition"
  7ff fcode>xt r> <> abort" FCode test: changed vendor token"

  ['] fcode-bind-test-bad-index catch 0= abort" FCode test: accepted 1000"
  ['] fcode-bind-test-negative-index catch 0= abort" FCode test: accepted -1"

  fcode-bind-test-system fcode-bind-test-stream
  here >r
  ['] new-token catch 0= abort" FCode test: accepted invalid new-token"
  here r> <> abort" FCode test: invalid token changed dictionary"
;

: test-fcode-bind ( -- )
  fcode-push-state
  alloc-fcode-table
  ['] (test-fcode-bind) catch >r
  free-fcode-table
  0 to fcode-table
  fcode-bind-test-first fcode-bind-test-stream
  ['] fcode! catch 0= if
    r> drop
    fcode-pop-state
    true abort" FCode test: accepted definition without active table"
  then
  fcode-pop-state
  r> throw
  ." FCode binding regression passed" cr
;

test-fcode-bind
