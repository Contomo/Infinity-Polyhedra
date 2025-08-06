# firmware

## File Structure

```raw
Core/             → STM32Cube HAL init (main.c, dma.c... etc i hope you know the drill)
led/              → render pipeline, mapping, animation, debug
polyhedron/       → geometric model + coordinate transforms
usb/              → CDC serial interface
config.h          → master tuning switches/flags
```

## Debugging

Enable logging in `config.h`:

```c
#define LED_DEBUG_RENDER
#define LED_DEBUG_ANIM
#define LED_DEBUG_MAPPING
```

You’ll get frame timing, animation duration, and heap usage stats over serial.

## Mapping

Custom remapping of virtual edge IDs to physical layout is done with:

```c
static const uint8_t USER_MAP[30] = { … };
static const bool USER_FLIP[30] = { … };
```

Currently theres no way to really save it at runtime yet, i just... recompiled and uploaded.  
*sort -> dump to console -> copy paste to file*