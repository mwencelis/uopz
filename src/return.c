/*
  +----------------------------------------------------------------------+
  | uopz                                                                 |
  +----------------------------------------------------------------------+
  | Copyright (c) Joe Watkins 2016                                       |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Joe Watkins <krakjoe@php.net>                                |
  +----------------------------------------------------------------------+
 */

#ifndef UOPZ_RETURN
#define UOPZ_RETURN

#include "php.h"
#include "uopz.h"

#include "util.h"
#include "return.h"

#include <Zend/zend_closures.h>

ZEND_EXTERN_MODULE_GLOBALS(uopz);

zend_bool uopz_set_return(zend_class_entry *clazz, zend_string *name, zval *value, zend_bool execute) { /* {{{ */
	HashTable *returns;
	uopz_return_t ret;
	zend_string *key = zend_string_tolower(name);

	if (clazz && uopz_find_function(&clazz->function_table, key, NULL) != SUCCESS) {
		uopz_exception(
			"failed to set return for %s::%s, the method does not exist",
			ZSTR_VAL(clazz->name),
			ZSTR_VAL(name));
		zend_string_release(key);
		return 0;
	}

	if (clazz) {
		returns = zend_hash_find_ptr(&UOPZ(returns), clazz->name);
	} else returns = zend_hash_index_find_ptr(&UOPZ(returns), 0);
	
	if (!returns) {
		ALLOC_HASHTABLE(returns);
		zend_hash_init(returns, 8, NULL, uopz_return_free, 0);
		if (clazz) {
			zend_hash_update_ptr(&UOPZ(returns), clazz->name, returns);
		} else zend_hash_index_update_ptr(&UOPZ(returns), 0, returns);
	}

	memset(&ret, 0, sizeof(uopz_return_t));
	
	ret.clazz = clazz;
	ret.function = zend_string_copy(name);
	ZVAL_COPY(&ret.value, value);
	ret.flags = execute ? UOPZ_RETURN_EXECUTE : 0;
	
	zend_hash_update_mem(returns, key, &ret, sizeof(uopz_return_t));
	zend_string_release(key);

	if (clazz && clazz->parent) {
		if (uopz_find_method(clazz->parent, name, NULL) == SUCCESS) {
			return uopz_set_return(clazz->parent, name, value, execute);
		}
	}

	return 1;
} /* }}} */

zend_bool uopz_unset_return(zend_class_entry *clazz, zend_string *function) { /* {{{ */
	HashTable *returns;
	zend_string *key = zend_string_tolower(function);
	
	if (clazz) {
		returns = zend_hash_find_ptr(&UOPZ(returns), clazz->name);
	} else returns = zend_hash_index_find_ptr(&UOPZ(returns), 0);

	if (!returns || !zend_hash_exists(returns, key)) {
		zend_string_release(key);
		return 0;
	}

	zend_hash_del(returns, key);
	zend_string_release(key);

	return 1;
} /* }}} */

void uopz_get_return(zend_class_entry *clazz, zend_string *function, zval *return_value) { /* {{{ */
	HashTable *returns;
	uopz_return_t *ureturn;

	if (clazz) {
		returns = zend_hash_find_ptr(&UOPZ(returns), clazz->name);
	} else returns = zend_hash_index_find_ptr(&UOPZ(returns), 0);

	if (!returns) {
		return;
	}

	ureturn = zend_hash_find_ptr(returns, function);

	if (!ureturn) {
		return;
	}
	
	ZVAL_COPY(return_value, &ureturn->value);
} /* }}} */

uopz_return_t* uopz_find_return(zend_function *function) { /* {{{ */
	HashTable *returns = function->common.scope ? zend_hash_find_ptr(&UOPZ(returns), function->common.scope->name) :
												  zend_hash_index_find_ptr(&UOPZ(returns), 0);

	if (returns && function->common.function_name) {
		Bucket *bucket;

		ZEND_HASH_FOREACH_BUCKET(returns, bucket) {
			if (zend_string_equals_ci(function->common.function_name, bucket->key)) {
				return Z_PTR(bucket->val);
			}
		} ZEND_HASH_FOREACH_END();
	}

	return NULL;
} /* }}} */

void uopz_execute_return(uopz_return_t *ureturn, zend_execute_data *execute_data, zval *return_value) { /* {{{ */
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
	char *error = NULL;
	zval closure, 
		 rv,
		 *result = return_value ? return_value : &rv;
	const zend_function *overload = zend_get_closure_method_def(&ureturn->value);

	ZVAL_UNDEF(&rv);

	ureturn->flags ^= UOPZ_RETURN_BUSY;

	zend_create_closure(&closure, (zend_function*) overload, 
		ureturn->clazz, ureturn->clazz, Z_OBJ(EX(This)) ? &EX(This) : NULL);

	if (zend_fcall_info_init(&closure, 0, &fci, &fcc, NULL, &error) != SUCCESS) {
		uopz_exception("cannot use return value set for %s as function: %s",
			ZSTR_VAL(EX(func)->common.function_name), error);
		if (error) {
			efree(error);
		}
		goto _exit_uopz_execute_return;
	}

	if (zend_fcall_info_argp(&fci, EX_NUM_ARGS(), EX_VAR_NUM(0)) != SUCCESS) {
		uopz_exception("cannot set arguments for %s",
			ZSTR_VAL(EX(func)->common.function_name));
		goto _exit_uopz_execute_return;
	}

	fci.retval= result;
	
	if (zend_call_function(&fci, &fcc) == SUCCESS) {
		zend_fcall_info_args_clear(&fci, 1);

		if (!return_value) {
			if (!Z_ISUNDEF(rv)) {
				zval_ptr_dtor(&rv);
			}
		}
	}

_exit_uopz_execute_return:
	zval_ptr_dtor(&closure);

	ureturn->flags ^= UOPZ_RETURN_BUSY;
} /* }}} */

void uopz_return_free(zval *zv) { /* {{{ */
	uopz_return_t *ureturn = Z_PTR_P(zv);
	
	zend_string_release(ureturn->function);
	zval_ptr_dtor(&ureturn->value);
	efree(ureturn);
} /* }}} */

#endif	/* UOPZ_RETURN */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
