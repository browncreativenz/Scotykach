#include "hw/hardware.h"

using namespace spotykach;

void Hardware::ProcessPads()
{
    PadEvent e;
    while (pads.pop(e))
    {
        if (e.touched) { if (_on_touch) _on_touch(e.pad); }
        else if (_on_release) _on_release(e.pad);
    }
}
