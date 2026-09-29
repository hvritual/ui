# Pocket Model / Repeater / Virtual Collection v1

Issue: #38.

A model is a small explicit contract: count, stable non-zero key and delegate
bind callback. The model never exposes Engine objects.

Repeater is for bounded small collections such as Wi-Fi AP rows. It materializes
at most its configured max and destroys excess components when the model
shrinks.

VirtualCollection is for List/Grid/Scroll parents. The materialized window is:

visible range + overscan before/after

and is bounded by max_pool. Scrolling reuses the same component pool and invokes
the delegate only when index/key changes or an explicit model revision refresh
requires rebinding.

Selection and focus are stored as model keys, not recycled component handles.
They therefore survive scrolling outside the materialized window and are
cleared only when the key disappears from the model.

The runtime validates non-zero keys and duplicate keys within each materialized
window. Full-model key uniqueness remains a model contract; scanning the whole
model on every scroll would make work proportional to total data size and defeat
virtualization.

F5 fixtures cover 100 drinks, 500 logs and a small Wi-Fi AP repeater while
recording pool size, component count, bind/recycle counts, pool bytes and test
process peak RSS.


Virtual selection/focus is reapplied to every materialized component after
delegate binding. `component_for_key` is available for runtime diagnostics and
tests, but identity remains the stable model key rather than the recycled
component handle.
