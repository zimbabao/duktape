/*
 *  Calls.
 *
 *  Protected variants should avoid ever throwing an error.
 */

#include "duk_internal.h"

/* Prepare value stack for a method call through an object property.
 * May currently throw an error e.g. when getting the property.
 */
static void duk__call_prop_prep_stack(duk_context *ctx, int normalized_obj_index, int nargs) {
	DUK_DDDPRINT("duk__call_prop_prep_stack, normalized_obj_index=%d, nargs=%d, stacktop=%d",
	             normalized_obj_index, nargs, duk_get_top(ctx));

	/* [... key arg1 ... argN] */

	/* duplicate key */
	duk_dup(ctx, -nargs - 1);  /* Note: -nargs alone would fail for nargs == 0, this is OK */
	duk_get_prop(ctx, normalized_obj_index);

	DUK_DDDPRINT("func: %!T", duk_get_tval(ctx, -1));

	/* [... key arg1 ... argN func] */

	duk_replace(ctx, -nargs - 2);

	/* [... func arg1 ... argN] */

	duk_dup(ctx, normalized_obj_index);
	duk_insert(ctx, -nargs - 1);

	/* [... func this arg1 ... argN] */
}

void duk_call(duk_context *ctx, int nargs) {
	duk_hthread *thr = (duk_hthread *) ctx;
	int call_flags;
	int idx_func;
	int rc;

	DUK_ASSERT(ctx != NULL);
	DUK_ASSERT(thr != NULL);

	idx_func = duk_get_top(ctx) - nargs - 1;  /* must work for nargs <= 0 */
	if (idx_func < 0 || nargs < 0) {
		/* note that we can't reliably pop anything here */
		DUK_ERROR(thr, DUK_ERR_API_ERROR, "invalid call args");
	}

	/* awkward; we assume there is space for this */
	duk_push_undefined(ctx);
	duk_insert(ctx, idx_func + 1);

	call_flags = 0;  /* not protected, respect reclimit, not constructor */

	rc = duk_handle_call(thr,           /* thread */
	                     nargs,         /* num_stack_args */
	                     call_flags);   /* call_flags */
	DUK_UNREF(rc);
}

void duk_call_method(duk_context *ctx, int nargs) {
	duk_hthread *thr = (duk_hthread *) ctx;
	int call_flags;
	int idx_func;
	int rc;

	DUK_ASSERT(ctx != NULL);
	DUK_ASSERT(thr != NULL);

	idx_func = duk_get_top(ctx) - nargs - 2;  /* must work for nargs <= 0 */
	if (idx_func < 0 || nargs < 0) {
		/* note that we can't reliably pop anything here */
		DUK_ERROR(thr, DUK_ERR_API_ERROR, "invalid call args");
	}

	call_flags = 0;  /* not protected, respect reclimit, not constructor */

	rc = duk_handle_call(thr,           /* thread */
	                     nargs,         /* num_stack_args */
	                     call_flags);   /* call_flags */
	DUK_UNREF(rc);
}

void duk_call_prop(duk_context *ctx, int obj_index, int nargs) {
	/*
	 *  XXX: if duk_handle_call() took values through indices, this could be
	 *  made much more sensible.  However, duk_handle_call() needs to fudge
	 *  the 'this' and 'func' values to handle bound function chains, which
	 *  is now done "in-place", so this is not a trivial change.
	 */

	obj_index = duk_require_normalize_index(ctx, obj_index);  /* make absolute */

	duk__call_prop_prep_stack(ctx, obj_index, nargs);

	duk_call_method(ctx, nargs);
}

int duk_pcall(duk_context *ctx, int nargs) {
	duk_hthread *thr = (duk_hthread *) ctx;
	int call_flags;
	int idx_func;
	int rc;

	DUK_ASSERT(ctx != NULL);
	DUK_ASSERT(thr != NULL);

	idx_func = duk_get_top(ctx) - nargs - 1;  /* must work for nargs <= 0 */
	if (idx_func < 0 || nargs < 0) {
		/* We can't reliably pop anything here because the stack input
		 * shape is incorrect.  So we throw an error; if the caller has
		 * no catch point for this, a fatal error will occur.  Another
		 * alternative would be to just return an error.  But then the
		 * stack would be in an unknown state which might cause some
		 * very hard to diagnose problems later on.  Also note that even
		 * if we did not throw an error here, the underlying call handler
		 * might STILL throw an out-of-memory error or some other internal
		 * fatal error.
		 */
		DUK_ERROR(thr, DUK_ERR_API_ERROR, "invalid call args");
		return DUK_EXEC_ERROR;  /* unreachable */
	}

	/* awkward; we assume there is space for this */
	duk_push_undefined(ctx);
	duk_insert(ctx, idx_func + 1);

	call_flags = DUK_CALL_FLAG_PROTECTED;  /* protected, respect reclimit, not constructor */

	rc = duk_handle_call(thr,           /* thread */
	                     nargs,         /* num_stack_args */
	                     call_flags);   /* call_flags */

	return rc;
}

int duk_pcall_method(duk_context *ctx, int nargs) {
	duk_hthread *thr = (duk_hthread *) ctx;
	int call_flags;
	int idx_func;
	int rc;

	DUK_ASSERT(ctx != NULL);
	DUK_ASSERT(thr != NULL);

	idx_func = duk_get_top(ctx) - nargs - 2;  /* must work for nargs <= 0 */
	if (idx_func < 0 || nargs < 0) {
		/* See comments in duk_pcall(). */
		DUK_ERROR(thr, DUK_ERR_API_ERROR, "invalid call args");
		return DUK_EXEC_ERROR;  /* unreachable */
	}

	call_flags = DUK_CALL_FLAG_PROTECTED;  /* protected, respect reclimit, not constructor */

	rc = duk_handle_call(thr,           /* thread */
	                     nargs,         /* num_stack_args */
	                     call_flags);   /* call_flags */

	return rc;
}

static int duk__pcall_prop_raw(duk_context *ctx) {
	int obj_index;
	int nargs;

	/* Get the original arguments.  Note that obj_index may be a relative
	 * index so the stack must have the same top when we use it.
	 */

	obj_index = duk_get_int(ctx, -2);
	nargs = duk_get_int(ctx, -1);
	duk_pop_2(ctx);

	obj_index = duk_require_normalize_index(ctx, obj_index);  /* make absolute */
	duk__call_prop_prep_stack(ctx, obj_index, nargs);
	duk_call_method(ctx, nargs);
	return 1;
}

int duk_pcall_prop(duk_context *ctx, int obj_index, int nargs) {
	/*
	 *  Must be careful to catch errors related to value stack manipulation
	 *  and property lookup, not just the call itself.
	 */

	duk_push_int(ctx, obj_index);
	duk_push_int(ctx, nargs);

	/* Inputs: explicit arguments (nargs), +1 for key, +2 for obj_index/nargs passing.
	 * If the value stack does not contain enough args, an error is thrown; this matches
	 * behavior of the other protected call API functions.
	 */
	return duk_safe_call(ctx, duk__pcall_prop_raw, nargs + 1 + 2 /*nargs*/, 1 /*nrets*/);
}

int duk_safe_call(duk_context *ctx, duk_safe_call_function func, int nargs, int nrets) {
	duk_hthread *thr = (duk_hthread *) ctx;
	int rc;

	DUK_ASSERT(ctx != NULL);
	DUK_ASSERT(thr != NULL);

	if (duk_get_top(ctx) < nargs || nrets < 0) {
		/* See comments in duk_pcall(). */
		DUK_ERROR(thr, DUK_ERR_API_ERROR, "invalid call args");
		return DUK_EXEC_ERROR;  /* unreachable */
	}

	rc = duk_handle_safe_call(thr,           /* thread */
	                          func,          /* func */
	                          nargs,         /* num_stack_args */
	                          nrets);        /* num_stack_res */

	return rc;
}

void duk_new(duk_context *ctx, int nargs) {
	/*
	 *  There are two [[Construct]] operations in the specification:
	 *
	 *    - E5 Section 13.2.2: for Function objects
	 *    - E5 Section 15.3.4.5.2: for "bound" Function objects
	 *
	 *  The chain of bound functions is resolved in Section 15.3.4.5.2,
	 *  with arguments "piling up" until the [[Construct]] internal
	 *  method is called on the final, actual Function object.  Note
	 *  that the "prototype" property is looked up *only* from the
	 *  final object, *before* calling the constructor.
	 *
	 *  Currently we follow the bound function chain here to get the
	 *  "prototype" property value from the final, non-bound function.
	 *  However, we let duk_handle_call() handle the argument "piling"
	 *  when the constructor is called.  The bound function chain is
	 *  thus now processed twice.
	 *
	 *  When constructing new Array instances, an unnecessary object is
	 *  created and discarded now: the standard [[Construct]] creates an
	 *  object, and calls the Array constructor.  The Array constructor
	 *  returns an Array instance, which is used as the result value for
	 *  the "new" operation; the object created before the Array constructor
	 *  call is discarded.
	 *
	 *  This would be easy to fix, e.g. by knowing that the Array constructor
	 *  will always create a replacement object and skip creating the fallback
	 *  object in that case.  FIXME.
	 *
	 *  Note: functions called via "new" need to know they are called as a
	 *  constructor.  For instance, built-in constructors behave differently
	 *  depending on how they are called.
	 */

	/* FIXME: should this go to duk_js_call.c? it implements core semantics. */

	duk_hthread *thr = (duk_hthread *) ctx;
	duk_hobject *proto;
	duk_hobject *cons;
	duk_hobject *fallback;
	int idx_cons;
	int call_flags;
	int rc;

	/* [... constructor arg1 ... argN] */

	idx_cons = duk_require_normalize_index(ctx, -nargs - 1);

	DUK_DDDPRINT("top=%d, nargs=%d, idx_cons=%d", duk_get_top(ctx), nargs, idx_cons);

	/* FIXME: code duplication */

	/*
	 *  Figure out the final, non-bound constructor, to get "prototype"
	 *  property.
	 */

	duk_dup(ctx, idx_cons);
	for (;;) {
		cons = duk_get_hobject(ctx, -1);
		if (cons == NULL || !DUK_HOBJECT_HAS_CONSTRUCTABLE(cons)) {
			/* Checking constructability from anything else than the
			 * initial constructor is not strictly necessary, but a
			 * nice sanity check.
			 */
			goto not_constructable;
		}
		if (!DUK_HOBJECT_HAS_BOUND(cons)) {
			break;
		}
		duk_get_prop_stridx(ctx, -1, DUK_STRIDX_INT_TARGET);  /* -> [... cons target] */
		duk_remove(ctx, -2);                                  /* -> [... target] */
	}
	DUK_ASSERT(cons != NULL && !DUK_HOBJECT_HAS_BOUND(cons));

	/* [... constructor arg1 ... argN final_cons] */

	/*
	 *  Create "fallback" object to be used as the object instance,
	 *  unless the constructor returns a replacement value.
	 *  Its internal prototype needs to be set based on "prototype"
	 *  property of the constructor.
	 */

	duk_push_object(ctx);  /* class Object, extensible */

	/* [... constructor arg1 ... argN final_cons fallback] */

	duk_get_prop_stridx(ctx, -2, DUK_STRIDX_PROTOTYPE);
	proto = duk_get_hobject(ctx, -1);
	if (!proto) {
		DUK_DDDPRINT("constructor has no 'prototype' property, or value not an object "
		             "-> leave standard Object prototype as fallback prototype");
	} else {
		DUK_DDDPRINT("constructor has 'prototype' property with object value "
		             "-> set fallback prototype to that value: %!iO", proto);
		fallback = duk_get_hobject(ctx, -2);
		DUK_ASSERT(fallback != NULL);
		DUK_HOBJECT_SET_PROTOTYPE_UPDREF(thr, fallback, proto);
	}
	duk_pop(ctx);

	/* [... constructor arg1 ... argN final_cons fallback] */

	/*
	 *  Manipulate callstack for the call.
	 */

	duk_dup_top(ctx);
	duk_insert(ctx, idx_cons + 1);  /* use fallback as 'this' value */
	duk_insert(ctx, idx_cons);      /* also stash it before constructor,
	                                 * in case we need it (as the fallback value)
	                                 */
	duk_pop(ctx);                   /* pop final_cons */


	/* [... fallback constructor fallback(this) arg1 ... argN];
	 * Note: idx_cons points to first 'fallback', not 'constructor'.
	 */

	DUK_DDDPRINT("before call, idx_cons+1 (constructor) -> %!T, idx_cons+2 (fallback/this) -> %!T, "
	             "nargs=%d, top=%d",
	             duk_get_tval(ctx, idx_cons + 1), duk_get_tval(ctx, idx_cons + 2),
	             nargs, duk_get_top(ctx));

	/*
	 *  Call the constructor function (called in "constructor mode").
	 */

	call_flags = DUK_CALL_FLAG_CONSTRUCTOR_CALL;  /* not protected, respect reclimit, is a constructor call */

	rc = duk_handle_call(thr,           /* thread */
	                     nargs,         /* num_stack_args */
	                     call_flags);   /* call_flags */
	DUK_UNREF(rc);

	/* [... fallback retval] */

	DUK_DDDPRINT("constructor call finished, rc=%d, fallback=%!iT, retval=%!iT",
	             rc, duk_get_tval(ctx, -2), duk_get_tval(ctx, -1));

	/*
	 *  Determine whether to use the constructor return value as the created
	 *  object instance or not.
	 */

	if (duk_is_object(ctx, -1)) {
		duk_remove(ctx, -2);
	} else {
		duk_pop(ctx);
	}

	/*
	 *  Augment created errors upon creation (not when they are thrown or
	 *  rethrown).  __FILE__ and __LINE__ are not desirable here; the call
	 *  stack reflects the caller which is correct.
	 */

#ifdef DUK_USE_AUGMENT_ERROR_CREATE
	duk_err_augment_error_create(thr, thr, NULL, 0, 1 /*noblame_fileline*/);
#endif

	/* [... retval] */

	return;

 not_constructable:
	DUK_ERROR(thr, DUK_ERR_TYPE_ERROR, "not constructable");
}

int duk_is_constructor_call(duk_context *ctx) {
	duk_hthread *thr = (duk_hthread *) ctx;
	duk_activation *act;

	/* For user code this could just return 1 (strict) always
	 * because all Duktape/C functions are considered strict,
	 * and strict is also the default when nothing is running.
	 * However, Duktape may call this function internally when
	 * the current activation is an Ecmascript function, so
	 * this cannot be replaced by a 'return 1'.
	 */

	DUK_ASSERT(ctx != NULL);
	DUK_ASSERT(thr != NULL);
	DUK_ASSERT_DISABLE(thr->callstack_top >= 0);

	if (thr->callstack_top <= 0) {
		return 1;  /* strict by default */
	}

	act = thr->callstack + thr->callstack_top - 1;
	return ((act->flags & DUK_ACT_FLAG_CONSTRUCT) != 0 ? 1 : 0);
}

int duk_is_strict_call(duk_context *ctx) {
	duk_hthread *thr = (duk_hthread *) ctx;
	duk_activation *act;

	DUK_ASSERT(ctx != NULL);
	DUK_ASSERT(thr != NULL);
	DUK_ASSERT_DISABLE(thr->callstack_top >= 0);

	if (thr->callstack_top <= 0) {
		return 0;
	}

	act = thr->callstack + thr->callstack_top - 1;
	return ((act->flags & DUK_ACT_FLAG_STRICT) != 0 ? 1 : 0);
}

/*
 *  Duktape/C function magic
 */

int duk_get_magic(duk_context *ctx) {
	duk_hthread *thr = (duk_hthread *) ctx;
	duk_activation *act;
	duk_hobject *func;

	DUK_ASSERT(ctx != NULL);
	DUK_ASSERT(thr != NULL);
	DUK_ASSERT_DISABLE(thr->callstack_top >= 0);

	if (thr->callstack_top <= 0) {
		return 0;
	}

	act = thr->callstack + thr->callstack_top - 1;
	func = act->func;
	DUK_ASSERT(func != NULL);

	if (DUK_HOBJECT_IS_NATIVEFUNCTION(func)) {
		duk_hnativefunction *nf = (duk_hnativefunction *) func;
		return (int) nf->magic;
	}

	return 0;
}

