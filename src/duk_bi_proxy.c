/*
 *  Proxy built-in (ES6 draft)
 */

#include "duk_internal.h"

#if defined(DUK_USE_ES6_PROXY)
duk_ret_t duk_bi_proxy_constructor(duk_context *ctx) {
	duk_hobject *h_target;
	duk_hobject *h_handler;

	if (!duk_is_constructor_call(ctx)) {
		return DUK_RET_TYPE_ERROR;
	}

	/* Reject a proxy object as the target because it would need
	 * special handler in property lookups.  (ES6 has no such restriction)
	 */
	h_target = duk_require_hobject(ctx, 0);
	DUK_ASSERT(h_target != NULL);
	if (DUK_HOBJECT_HAS_SPECIAL_PROXYOBJ(h_target)) {
		return DUK_RET_TYPE_ERROR;
	}

	/* Reject a proxy object as the handler because it would cause
	 * potentially unbounded recursion.  (ES6 has no such restriction)
	 */
	h_handler = duk_require_hobject(ctx, 1);
	DUK_ASSERT(h_handler != NULL);
	if (DUK_HOBJECT_HAS_SPECIAL_PROXYOBJ(h_handler)) {
		return DUK_RET_TYPE_ERROR;
	}

	/* XXX: the returned value is exotic in ES6 (draft), but we use a
	 * simple object here with no prototype.
	 */
	(void) duk_push_object_helper_proto(ctx,
	                                    DUK_HOBJECT_FLAG_SPECIAL_PROXYOBJ |
	                                    DUK_HOBJECT_CLASS_AS_FLAGS(DUK_HOBJECT_CLASS_OBJECT),  /* FIXME: class? */
	                                    NULL);

	/* XXX: no callable check/handling now */
	/* XXX: with no prototype, [[DefaultValue]] coercion will fail, which is confusing */

	DUK_ASSERT_TOP(ctx, 3);

	/* Proxy target */
	duk_dup(ctx, 0);
	duk_def_prop_stridx(ctx, -2, DUK_STRIDX_INT_TARGET, DUK_PROPDESC_FLAGS_WC);

	/* Proxy handler */
	duk_dup(ctx, 1);
	duk_def_prop_stridx(ctx, -2, DUK_STRIDX_INT_HANDLER, DUK_PROPDESC_FLAGS_WC);

	return 1;
}
#else  /* DUK_UES_ES6_PROXY */
duk_ret_t duk_bi_proxy_constructor(duk_context *ctx) {
	DUK_UNREF(ctx);
	return DUK_RET_UNSUPPORTED_ERROR;
}
#endif  /* DUK_USE_ES6_PROXY */

#if defined(DUK_USE_ES6_PROXY)
duk_ret_t duk_bi_proxy_constructor_revocable(duk_context *ctx) {
	DUK_UNREF(ctx);
	return DUK_RET_UNIMPLEMENTED_ERROR;  /*FIXME*/
}
#else  /* DUK_UES_ES6_PROXY */
duk_ret_t duk_bi_proxy_constructor_revocable(duk_context *ctx) {
	DUK_UNREF(ctx);
	return DUK_RET_UNSUPPORTED_ERROR;
}
#endif  /* DUK_USE_ES6_PROXY */
