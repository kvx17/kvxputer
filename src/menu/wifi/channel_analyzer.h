#pragma once

#if !defined(LITE_VERSION)

// Real-time 2.4GHz channel utilization analyzer.
// Hops channels 1-11 in promiscuous mode, estimates per-channel airtime load,
// and draws vertical EVA-01 colored bars with peak hold and a signal meter.
void channel_analyzer_setup();

#endif
