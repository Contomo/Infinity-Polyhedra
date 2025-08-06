import pygame


#===============[ PS4 CONTROLLER MAPPING ]===============

UPDATES_PER_SEC = 5 # 5 Hz Serial spam rate

#-----------------------------------------------

# PS4 Controller (pygame 2.x) button indices
BTN_CROSS       = 0   # "Cross"
BTN_CIRCLE      = 1   # "Circle"
BTN_SQUARE      = 2   # "Square"
BTN_TRIANGLE    = 3   # "Triangle"
BTN_SHARE       = 4
BTN_PS          = 5
BTN_OPTIONS     = 6
BTN_L_STICK     = 7
BTN_R_STICK     = 8
BTN_L1          = 9   # Left Bumper
BTN_R1          = 10  # Right Bumper
BTN_DPAD_UP     = 11
BTN_DPAD_DOWN   = 12
BTN_DPAD_LEFT   = 13
BTN_DPAD_RIGHT  = 14
BTN_TOUCHPAD    = 15

DEBOUNCE_MS = 0.02      # for buttons
REPEAT_DELAY = 0.30     # for buttons

#-----------------------------------------------

# PS4 Controller (pygame 2.x) Axis indices
LEFT_STICK_X        = 0 # Left - Right  (-1 to 1)
LEFT_STICK_Y        = 1 # Down - Up     (-1 to 1)
RIGHT_STICK_X       = 2 # Left - Right  (-1 to 1)
RIGHT_STICK_Y       = 3 # Down - Up     (-1 to 1)

STICK_SENSE         = 5
STICK_DEADZONE      = 0.05

#-----------------------------------------------

LEFT_TRIGGER        = 4 # Press/In (0 to 1)
RIGHT_TRIGGER       = 5 # Press/In (0 to 1)

TRIGGER_SENSE       = 10
TRIGGER_DEADZONE    = 0.05

#===========================================================


GYRO_SEND_RATE = 5

#===============[ SERIAL SETTINGS ]===============

BAUD = 115200
FAST_RETRY = 2.0
SLOW_RETRY = 10.0

METRICS_HISTORY = 10

#===========================================================



# Named MCU commands
COMMANDS = {
    'face':   'f',      # change face index
    'bar':    'b',      # change bar index
    'edge':   'e',      # change edge slot
    'mode':   'm++',      # switch mode
    'reverse':'r',      # flip/reverse edge
    'save':   'save',   # persist mapping
    'help':   'help',   # list commands
    'dump':   '#dumpgeo#',  # dump current model
    'print':  'g',      # print sample poly (printPolys)
    'hue':    'h', 
}

# Joystick button → command key mapping
JOYSTICK_BUTTON_MAPPING = {
    BTN_SQUARE:   COMMANDS['save'],
    BTN_CROSS:    COMMANDS['reverse'],
    BTN_L1:       'f--',  # face −1
    BTN_R1:       'f++',  # face +1
    BTN_TRIANGLE: COMMANDS['print'],
    BTN_OPTIONS:  COMMANDS['dump'],
    BTN_CIRCLE:  COMMANDS['mode']
}

# Keyboard key → command key mapping
KEYBOARD_MAPPING = {
    pygame.K_f:      COMMANDS['face'],
    pygame.K_b:      COMMANDS['bar'],
    pygame.K_e:      COMMANDS['edge'],
    pygame.K_m:      COMMANDS['mode'],
    pygame.K_r:      COMMANDS['reverse'],
    pygame.K_s:      COMMANDS['save'],
    pygame.K_h:      COMMANDS['help'],
    pygame.K_d:      COMMANDS['dump'],
    pygame.K_g:      COMMANDS['print'],
}

# command → axis index
AXIS_MAPPING = {
    COMMANDS['bar']:    LEFT_STICK_X,
    COMMANDS['edge']:   RIGHT_STICK_X,
}

TRIGGER_MAPPING = {
    COMMANDS['hue']: [
        (LEFT_TRIGGER,  -1),  # LT für negativen Delta
        (RIGHT_TRIGGER, +1),  # RT für positiven Delta
    ],
}
