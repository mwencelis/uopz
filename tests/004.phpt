--TEST--
uopz_set_mock
--SKIPIF--
<?php include("skipif.inc") ?>
--INI--
uopz.disable=0
--FILE--
<?php
class Foo {
	static $prop = 1;

	public static function method() {
		return -1;	
	}
}

class Bar {
	static $prop = 2;
}

uopz_set_mock(Foo::class, Bar::class);

var_dump(new Foo(), Foo::$prop);

var_dump(Foo::method());

uopz_unset_mock(Foo::class);

uopz_set_mock(Bar::class, new Foo);

var_dump(new Bar());
?>
--EXPECTF--
object(Bar)#%d (0) {
}
int(1)
int(-1)
object(Foo)#%d (0) {
}
