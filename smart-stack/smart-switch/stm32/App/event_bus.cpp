#include "event_bus.hpp"
#include "executor.hpp"

namespace app {

EventBus& EventBus::instance() {
    static EventBus b;
    return b;
}

bool EventBus::subscribe(EventId evt, EventHandler h, void* ctx) {
    for (auto& s : subs_) {
        if (s.used && s.evt == evt && s.h == h && s.ctx == ctx) return true; // already
    }
    for (auto& s : subs_) {
        if (!s.used) { s = {evt, h, ctx, true}; return true; }
    }
    return false;
}

bool EventBus::unsubscribe(EventId evt, EventHandler h, void* ctx) {
    for (auto& s : subs_) {
        if (s.used && s.evt == evt && s.h == h && s.ctx == ctx) { s.used = false; return true; }
    }
    return false;
}

struct EvtThunkCtx { EventId evt; EventHandler h; void* ctx; const void* payload; uint16_t size; };
static void evt_thunk(void* p) {
    auto* c = static_cast<EvtThunkCtx*>(p);
    c->h(c->evt, c->payload, c->size, c->ctx);
}

bool EventBus::publish(EventId evt, const void* payload, uint16_t size) {
    bool ok = true;
    for (auto& s : subs_) {
        if (s.used && s.evt == evt && s.h) {
            EvtThunkCtx c{evt, s.h, s.ctx, payload, size};
            Task t{&evt_thunk, &c};
            // NOTE: c lives on stack; need to copy. Use small inline copying by posting the handler directly.
            // Alternative: call handler inline since we're already in executor context typically.
            // For safety, call handler directly here.
            s.h(evt, payload, size, s.ctx);
        }
    }
    return ok;
}

static void evt_thunk_isr(void* p) {
    auto* c = static_cast<EvtThunkCtx*>(p);
    c->h(c->evt, c->payload, c->size, c->ctx);
}

bool EventBus::publishFromISR(EventId evt, const void* payload, uint16_t size) {
    bool any = false;
    for (auto& s : subs_) {
        if (s.used && s.evt == evt && s.h) {
            // Copy context into a tiny heapless holder per post (on stack unsafe). Build a small static slot? Simpler: directly post handler/ctx via task.
            // We can post the handler by capturing (h,ctx,payload,size) in a small struct stored in a static ring; reuse TaskQueue directly.
            struct Local { EventId e; EventHandler h; void* ctx; const void* pl; uint16_t sz; };
            static Local locals[4]; static volatile uint32_t idx;
            uint32_t i = idx++ % 4;
            locals[i] = {evt, s.h, s.ctx, payload, size};
            auto thunk = [](void* p){ auto* L = static_cast<Local*>(p); L->h(L->e, L->pl, L->sz, L->ctx); };
            Task t{thunk, &locals[i]};
            any |= Executor::instance().postFromISR(t);
        }
    }
    return any;
}

} // namespace app
