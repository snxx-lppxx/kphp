@kphp_should_fail
KPHP_ENABLE_FFI=1
/ffi->new\(\): line 1: syntax error, unexpected INT_CONSTANT/
<?php

$cdef = FFI::cdef('
  struct Foo { bool field; };
');

$foo = $cdef->new('123');
