/*
 *  Augmenting errors at their creation site and their throw site.
 *
 *  When errors are created, traceback data is added by built-in code
 *  and a user error handler (if defined) can process or replace the
 *  error.  Similarly, when errors are thrown, a user error handler
 *  (if defined) can process or replace the error.
 *
 *  Augmentation and other processing at error creation time is nice
 *  because an error is only created once, but it may be thrown and
 *  rethrown multiple times.  User error handler registered for processing
 *  an error at its throw site must be careful to handle rethrowing in
 *  a useful manner.
 *
 *  Error augmentation may throw an internal error (e.g. alloc error).
 *
 *  Ecmascript allows throwing any values, so all values cannot be
 *  augmented.  Currently, the built-in augmentation at error creation
 *  only augments error values which are Error instances (= have the
 *  built-in Error.prototype in their prototype chain) and are also
 *  extensible.  User error handlers have no limitations in this respect.
 */

#include "duk_internal.h"

/*
 *  Helper for calling a user error handler.
 *
 *  'thr' must be the currently active thread; the error handler is called
 *  in its context.  The valstack of 'thr' must have the error value on
 *  top, and will be replaced by another error value based on the return
 *  value of the error handler.
 *
 *  The helper calls duk_handle_call() recursively in protected mode.
 *  Before that call happens, no longjmps should happen; as a consequence,
 *  we must assume that the valstack contains enough temporary space for
 *  arguments and such.
 *
 *  While the error handler runs, any errors thrown will not trigger a
 *  recursive error handler call (this is implemented using a heap level
 *  flag which will "follow" through any coroutines resumed inside the
 *  error handler).  If the error handler is not callable or throws an
 *  error, the resulting error replaces the original error (for Duktape
 *  internal errors, duk_error_throw.c further substitutes this error with
 *  a DoubleError which is not ideal).  This would be easy to change and
 *  even signal to the caller.
 *
 *  The user error handler is stored in 'Duktape.errcreate' or
 *  'Duktape.errthrow' depending on whether we're augmenting the error at
 *  creation or throw time.  There are several alternatives to this approach,
 *  see doc/error-objects.txt for discussion.
 *
 *  Note: since further longjmp()s may occur while calling the error handler
 *  (for many reasons, e.g. a labeled 'break' inside the handler), the
 *  caller can make no assumptions on the thr->heap->lj state after the
 *  call (this affects especially duk_error_throw.c).  This is not an issue
 *  as long as the caller writes to the lj state only after the error handler
 *  finishes.
 */

#if defined(DUK_USE_ERRTHROW) || defined(DUK_USE_ERRCREATE)
static void duk__err_augment_user(duk_hthread *thr, int stridx_cb) {
	duk_context *ctx = (duk_context *) thr;
	duk_tval *tv_hnd;
	int call_flags;
	int rc;

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(thr->heap != NULL);
	DUK_ASSERT(stridx_cb >= 0 && stridx_cb < DUK_HEAP_NUM_STRINGS);

	if (DUK_HEAP_HAS_ERRHANDLER_RUNNING(thr->heap)) {
		DUK_DDPRINT("recursive call to error handler, ignore");
		return;
	}

	/*
	 *  Check whether or not we have an error handler.
	 *
	 *  We must be careful of not triggering an error when looking up the
	 *  property.  For instance, if the property is a getter, we don't want
	 *  to call it, only plain values are allowed.  The value, if it exists,
	 *  is not checked.  If the value is not a function, a TypeError happens
	 *  when it is called and that error replaces the original one.
	 */

	DUK_ASSERT_VALSTACK_SPACE(thr, 4);  /* 3 entries actually needed below */

	/* [ ... errval ] */

	if (thr->builtins[DUK_BIDX_DUKTAPE] == NULL) {
		/* When creating built-ins, some of the built-ins may not be set
		 * and we want to tolerate that when throwing errors.
		 */
		DUK_DDPRINT("error occurred when DUK_BIDX_DUKTAPE is NULL, ignoring");
		return;
	}
	tv_hnd = duk_hobject_find_existing_entry_tval_ptr(thr->builtins[DUK_BIDX_DUKTAPE],
	                                                  thr->strs[stridx_cb]);
	if (tv_hnd == NULL) {
		DUK_DDPRINT("error handler does not exist or is not a plain value: %!T", tv_hnd);
		return;
	}
	DUK_DDDPRINT("error handler dump (callability not checked): %!T", tv_hnd);
	duk_push_tval(ctx, tv_hnd);

	/* [ ... errval errhandler ] */

	duk_insert(ctx, -2);  /* -> [ ... errhandler errval ] */
	duk_push_undefined(ctx);
	duk_insert(ctx, -2);  /* -> [ ... errhandler undefined(= this) errval ] */

	/* [ ... errhandler undefined errval ] */

	/*
	 *  DUK_CALL_FLAG_IGNORE_RECLIMIT causes duk_handle_call() to ignore C
	 *  recursion depth limit (and won't increase it either).  This is
	 *  dangerous, but useful because it allows the error handler to run
	 *  even if the original error is caused by C recursion depth limit.
	 *
	 *  The heap level DUK_HEAP_FLAG_ERRHANDLER_RUNNING is set for the
	 *  duration of the error handler and cleared afterwards.  This flag
	 *  prevents the error handler from running recursively.  The flag is
	 *  heap level so that the flag properly controls even coroutines
	 *  launched by an error handler.  Since the flag is heap level, it is
	 *  critical to restore it correctly.
	 *
	 *  We ignore errors now: a success return and an error value both
	 *  replace the original error value.  (This would be easy to change.)
	 */

	DUK_ASSERT(!DUK_HEAP_HAS_ERRHANDLER_RUNNING(thr->heap));  /* since no recursive error handler calls */
	DUK_HEAP_SET_ERRHANDLER_RUNNING(thr->heap);

	call_flags = DUK_CALL_FLAG_PROTECTED |
	             DUK_CALL_FLAG_IGNORE_RECLIMIT;  /* protected, ignore reclimit, not constructor */

	rc = duk_handle_call(thr,
	                     1,            /* num args */
	                     call_flags);  /* call_flags */
	DUK_UNREF(rc);  /* no need to check now: both success and error are OK */

	DUK_ASSERT(DUK_HEAP_HAS_ERRHANDLER_RUNNING(thr->heap));
	DUK_HEAP_CLEAR_ERRHANDLER_RUNNING(thr->heap);

	/* [ ... errval ] */
}
#endif  /* DUK_USE_ERRTHROW || DUK_USE_ERRHANDLE */

/*
 *  Add tracedata to an error on the stack top.
 */

#ifdef DUK_USE_TRACEBACKS
static void duk__add_traceback(duk_hthread *thr, duk_hthread *thr_callstack, const char *filename, int line, int noblame_fileline) {
	duk_context *ctx = (duk_context *) thr;
	int depth;
	int i, i_min;
	int arr_idx;
	double d;

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(thr_callstack != NULL);
	DUK_ASSERT(ctx != NULL);

	/* [ ... error ] */

	/*
	 *  The traceback format is pretty arcane in an attempt to keep it compact
	 *  and cheap to create.  It may change arbitrarily from version to version.
	 *  It should be decoded/accessed through version specific accessors only.
	 *
	 *  See doc/error-objects.txt.
	 */

	DUK_DDDPRINT("adding traceback to object: %!T", duk_get_tval(ctx, -1));

	duk_push_array(ctx);  /* XXX: specify array size, as we know it */
	arr_idx = 0;

	/* filename/line from C macros (__FILE__, __LINE__) are added as an
	 * entry with a special format: (string, number).  The number contains
	 * the line and flags.
	 */

	/* FIXME: optimize: allocate an array part to the necessary size (upwards
	 * estimate) and fill in the values directly into the array part; finally
	 * update 'length'.
	 */

	/* FIXME: using duk_put_prop_index() would cause obscure error cases when Array.prototype
	 * has write-protected array index named properties.  This was seen as DoubleErrors
	 * in e.g. some test262 test cases.  Using duk_def_prop_index() is better but currently
	 * there is no fast path variant for that; the current implementation interns the array
	 * index as a string.  This can be fixed directly, or perhaps the traceback can be fixed
	 * altogether to fill in the tracedata directly into the array part.
	 */

	/* [ ... error arr ] */

	if (filename) {
		duk_push_string(ctx, filename);
		duk_def_prop_index(ctx, -2, arr_idx, DUK_PROPDESC_FLAGS_WEC);
		arr_idx++;

		d = (noblame_fileline ? ((double) DUK_TB_FLAG_NOBLAME_FILELINE) * DUK_DOUBLE_2TO32 : 0.0) +
		    (double) line;
		duk_push_number(ctx, d);
		duk_def_prop_index(ctx, -2, arr_idx, DUK_PROPDESC_FLAGS_WEC);
		arr_idx++;
	}

	/* traceback depth doesn't take into account the filename/line
	 * special handling above (intentional)
	 */
	depth = DUK_USE_TRACEBACK_DEPTH;
	i_min = (thr_callstack->callstack_top > (size_t) depth ? (int) (thr_callstack->callstack_top - depth) : 0);
	DUK_ASSERT(i_min >= 0);

	/* [ ... error arr ] */

	for (i = thr_callstack->callstack_top - 1; i >= i_min; i--) {
		int pc;

		/*
		 *  Note: each API operation potentially resizes the callstack,
		 *  so be careful to re-lookup after every operation.  Currently
		 *  these is no issue because we don't store a temporary 'act'
		 *  pointer at all.  (This would be a non-issue if we operated
		 *  directly on the array part.)
		 */

		/* [... arr] */

		DUK_ASSERT(thr_callstack->callstack[i].func != NULL);
		DUK_ASSERT(thr_callstack->callstack[i].pc >= 0);

		/* add function */
		duk_push_hobject(ctx, thr_callstack->callstack[i].func);  /* -> [... arr func] */
		duk_def_prop_index(ctx, -2, arr_idx, DUK_PROPDESC_FLAGS_WEC);
		arr_idx++;

		/* add a number containing: pc, activation flags */

		/* Add a number containing: pc, activation flag
		 *
		 * PC points to next instruction, find offending PC.  Note that
		 * PC == 0 for native code.
		 */
		pc = thr_callstack->callstack[i].pc;
		if (pc > 0) {
			pc--;
		}
		DUK_ASSERT(pc >= 0 && (double) pc < DUK_DOUBLE_2TO32);  /* assume PC is at most 32 bits and non-negative */
		d = ((double) thr_callstack->callstack[i].flags) * DUK_DOUBLE_2TO32 + (double) pc;
		duk_push_number(ctx, d);  /* -> [... arr num] */
		duk_def_prop_index(ctx, -2, arr_idx, DUK_PROPDESC_FLAGS_WEC);
		arr_idx++;
	}

	/* FIXME: set with duk_hobject_set_length() when tracedata is filled directly */
	duk_push_int(ctx, (int) arr_idx);
	duk_def_prop_stridx(ctx, -2, DUK_STRIDX_LENGTH, DUK_PROPDESC_FLAGS_WC);

	/* [ ... error arr ] */

	duk_def_prop_stridx(ctx, -2, DUK_STRIDX_TRACEDATA, DUK_PROPDESC_FLAGS_WEC);  /* -> [ ... error ] */
}
#endif  /* DUK_USE_TRACEBACKS */

#if defined(DUK_USE_AUGMENT_ERROR_CREATE)
static void duk__err_augment_builtin_throw(duk_hthread *thr, duk_hthread *thr_callstack, const char *filename, int line, int noblame_fileline, duk_hobject *obj) {
	duk_context *ctx = (duk_context *) thr;
#ifdef DUK_USE_ASSERTIONS
	duk_int_t entry_top;
#endif

#ifdef DUK_USE_ASSERTIONS
	entry_top = duk_get_top(ctx);
#endif
	DUK_ASSERT(obj != NULL);

	DUK_UNREF(obj);  /* unreferenced w/o tracebacks */
	DUK_UNREF(ctx);  /* unreferenced w/ tracebacks */

#ifdef DUK_USE_TRACEBACKS
	/*
	 *  If tracebacks are enabled, the 'tracedata' property is the only
	 *  thing we need: 'fileName' and 'lineNumber' are virtual properties
	 *  which use 'tracedata'.
	 */

	if (duk_hobject_hasprop_raw(thr, obj, DUK_HTHREAD_STRING_TRACEDATA(thr))) {
		DUK_DDDPRINT("error value already has a 'tracedata' property, not modifying it");
	} else {
		duk__add_traceback(thr, thr_callstack, filename, line, noblame_fileline);
	}
#else
	/*
	 *  If tracebacks are disabled, 'fileName' and 'lineNumber' are added
	 *  as plain own properties.  Since Error.prototype has accessors of
	 *  the same name, we need to define own properties directly (cannot
	 *  just use e.g. duk_put_prop_stridx).  Existing properties are not
	 *  overwritten in case they already exist.
	 */

	if (filename && !noblame_fileline) {
		/* FIXME: file/line is disabled in minimal builds, so disable this too
		 * when appropriate.
		 */
		duk_push_string(ctx, filename);
		duk_def_prop_stridx(ctx, -2, DUK_STRIDX_FILE_NAME, DUK_PROPDESC_FLAGS_WC | DUK_PROPDESC_FLAG_NO_OVERWRITE);
		duk_push_int(ctx, line);
		duk_def_prop_stridx(ctx, -2, DUK_STRIDX_LINE_NUMBER, DUK_PROPDESC_FLAGS_WC | DUK_PROPDESC_FLAG_NO_OVERWRITE);
	} else if (thr_callstack->callstack_top > 0) {
		duk_activation *act;
		duk_hobject *func;
		duk_hbuffer *pc2line;

		act = thr_callstack->callstack + thr_callstack->callstack_top - 1;
		DUK_ASSERT(act >= thr_callstack->callstack && act < thr_callstack->callstack + thr_callstack->callstack_size);
		func = act->func;
		if (func) {
			int pc;
			duk_uint32_t line;

			/* PC points to next instruction, find offending PC.  Note that
			 * PC == 0 for native code.
			 */
			pc = act->pc;
			if (pc > 0) {
				pc--;
			}
			DUK_ASSERT(pc >= 0 && (double) pc < DUK_DOUBLE_2TO32);  /* assume PC is at most 32 bits and non-negative */
			act = NULL;  /* invalidated by pushes, so get out of the way */

			duk_push_hobject(ctx, func);

			/* [ ... error func ] */

			duk_get_prop_stridx(ctx, -1, DUK_STRIDX_FILE_NAME);
			duk_def_prop_stridx(ctx, -3, DUK_STRIDX_FILE_NAME, DUK_PROPDESC_FLAGS_WC | DUK_PROPDESC_FLAG_NO_OVERWRITE);
			if (DUK_HOBJECT_IS_COMPILEDFUNCTION(func)) {
#if 0
				duk_push_number(ctx, pc);
				duk_def_prop_stridx(ctx, -3, DUK_STRIDX_PC, DUK_PROPDESC_FLAGS_WC | DUK_PROPDESC_FLAGS_NO_OVERWRITE);
#endif

				duk_get_prop_stridx(ctx, -1, DUK_STRIDX_INT_PC2LINE);
				if (duk_is_buffer(ctx, -1)) {
					pc2line = duk_get_hbuffer(ctx, -1);
					DUK_ASSERT(pc2line != NULL);
					DUK_ASSERT(!DUK_HBUFFER_HAS_DYNAMIC(pc2line));
					line = duk_hobject_pc2line_query((duk_hbuffer_fixed *) pc2line, (duk_uint_fast32_t) pc);
					duk_push_number(ctx, (double) line); /* -> [ ... error func pc2line line ] */  /* FIXME: u32 */
					duk_def_prop_stridx(ctx, -4, DUK_STRIDX_LINE_NUMBER, DUK_PROPDESC_FLAGS_WC | DUK_PROPDESC_FLAG_NO_OVERWRITE);
				}
				duk_pop(ctx);
			} else {
				/* Native function, no relevant lineNumber. */
			}

			duk_pop(ctx);
		}
	}
#endif  /* DUK_USE_TRACEBACKS */

#ifdef DUK_USE_ASSERTIONS
	DUK_ASSERT(duk_get_top(ctx) == entry_top);
#endif
}
#endif  /* DUK_USE_AUGMENT_ERROR_CREATE */

/*
 *  Augment an error at creation time with tracedata/fileName/lineNumber
 *  and allow a user error handler (if defined) to process/replace the error.
 *  The error to be augmented is at the stack top.
 *
 *  thr: thread containing the error value
 *  thr_callstack: thread which should be used for generating callstack etc.
 *  filename: C __FILE__ related to the error
 *  line: C __LINE__ related to the error
 *  noblame_fileline: if true, don't fileName/line as error source, otherwise use traceback
 *                    (needed because user code filename/line are reported but internal ones
 *                    are not)
 *
 *  FIXME: rename noblame_fileline to flags field; combine it to some existing
 *  field (there are only a few call sites so this may not be worth it).
 */

#if defined(DUK_USE_AUGMENT_ERROR_CREATE)
void duk_err_augment_error_create(duk_hthread *thr, duk_hthread *thr_callstack, const char *filename, int line, int noblame_fileline) {
	duk_context *ctx = (duk_context *) thr;
	duk_hobject *obj;

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(thr_callstack != NULL);
	DUK_ASSERT(ctx != NULL);

	/* [ ... error ] */

	/*
	 *  Criteria for augmenting:
	 *
	 *   - augmentation enabled in build (naturally)
	 *   - error value internal prototype chain contains the built-in
	 *     Error prototype object (i.e. 'val instanceof Error')
	 *
	 *  Additional criteria for built-in augmenting:
	 *
	 *   - error value is an extensible object
	 */

	obj = duk_get_hobject(ctx, -1);
	if (!obj) {
		DUK_DDDPRINT("value is not an object, skip both built-in and user augment");
		return;
	}
	if (!duk_hobject_prototype_chain_contains(thr, obj, thr->builtins[DUK_BIDX_ERROR_PROTOTYPE])) {
		DUK_DDDPRINT("value is not an error instance, skip both built-in and user augment");
		return;
	}

	if (DUK_HOBJECT_HAS_EXTENSIBLE(obj)) {
		DUK_DDDPRINT("error meets criteria, built-in augment");
		duk__err_augment_builtin_throw(thr, thr_callstack, filename, line, noblame_fileline, obj);
	} else {
		DUK_DDDPRINT("error does not meet criteria, no built-in augment");
	}

	/* [ ... error ] */

#if defined(DUK_USE_ERRCREATE)
	duk__err_augment_user(thr, DUK_STRIDX_ERRCREATE);
#endif
}
#endif  /* DUK_USE_AUGMENT_ERROR_CREATE */

/*
 *  Augment an error at throw time; allow a user error handler (if defined)
 *  to process/replace the error.  The error to be augmented is at the
 *  stack top.
 */

#if defined(DUK_USE_AUGMENT_ERROR_THROW)
void duk_err_augment_error_throw(duk_hthread *thr) {
#if defined(DUK_USE_ERRTHROW)
	duk__err_augment_user(thr, DUK_STRIDX_ERRTHROW);
#endif  /* DUK_USE_ERRTHROW */
}
#endif  /* DUK_USE_AUGMENT_ERROR_THROW */
