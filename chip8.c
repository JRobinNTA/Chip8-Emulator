#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#include "SDL.h"

// SDL Container object
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
} sdl_t;

// Configuration object
typedef struct {
    uint32_t window_width;  // SDL window width
    uint32_t window_height; // SDL window height
    uint32_t fg_color;      // Foreground color RGBA8888
    uint32_t bg_color;      // Background color RGBA8888
    uint32_t scale_factor;  // Amount to scale a CHIP8 pixel by e.g. 20x will be a 20x larger window
    bool pixel_outlines;    // Draw pixel outlines
    uint32_t clock_speed;   // CHIP8 clock speed in Hz or number of instructions to execute per second
} config_t;

// Emulator states
typedef enum{
    QUIT,
    RUNNING,
    PAUSED,
} emulator_state_t;

//chip8 opcode format
typedef struct{
    uint16_t opcode;
    uint16_t NNN;   // 12 bit address/constant
    uint8_t NN;     // 8 bit constant
    uint8_t N;      // 4 bit constant
    uint8_t X;      // 4 bit register identifier
    uint8_t Y;      // 4 bit register identifier
} instruction_t;

// CHIP8 Machine object
typedef struct{
    emulator_state_t state;
    uint8_t ram[4096];      // 4KB of RAM
    bool display[64*32];    // 64x32 pixel display
    uint16_t stack[12];     // subroutine stack
    uint16_t *stack_ptr;      // stack pointer
    uint8_t V[16];          // 16 8-bit registers
    uint16_t PC;            // 16-bit program counter supposed to be 12-bit
    uint16_t I;             // 16-bit index register supposed to be 12-bit
    uint8_t delay_timer;    // delay timer deccrements at 60hz when >0
    uint8_t sound_timer;    // sound timer decrements at 60hz and plays tone when >0
    bool keypad[16];        // hexadecimal keypad 0x0-0xF
    const char *rom_name;   // current ROM name
    instruction_t inst;     // current instruction
    bool draw ;             // update the screen yes/no
} chip8_t;

//Initialize SDL
bool init_sdl(sdl_t *sdl, const config_t config){
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) !=0){
        SDL_Log("Could not initialize SDL subsystems! %s\n", SDL_GetError()); // Amount to scale a CHIP8 pixel by e.g. 20x will be a 20x larger window
        return false; // init failed
    }
    sdl->window = SDL_CreateWindow("CHIP8 Emulator", SDL_WINDOWPOS_CENTERED,
                                    SDL_WINDOWPOS_CENTERED,
                                    config.window_width*config.scale_factor,
                                    config.window_height*config.scale_factor,
                                    0);
    if(!sdl->window){
        SDL_Log("Could not create SDL window %s\n", SDL_GetError());
        return false; // init failed
    }
    sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_ACCELERATED);
    if(!sdl->renderer){
        SDL_Log("Could not create SDL renderer %s\n", SDL_GetError());
        return false; // init failed
    }
    return true; // init success
}

//Initialize CHIP8 machine
bool init_chip8(chip8_t *chip8, const char *rom_name){
    const uint32_t entry_point = 0x200; // CHIP8 ROM entry point
    const uint8_t font[] ={
        0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
        0x20, 0x60, 0x20, 0x20, 0x70, // 1
        0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
        0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
        0x90, 0x90, 0xF0, 0x10, 0x10, // 4
        0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
        0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
        0xF0, 0x10, 0x20, 0x40, 0x40, // 7
        0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
        0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
        0xF0, 0x90, 0xF0, 0x90, 0x90, // A
        0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
        0xF0, 0x80, 0x80, 0x80, 0xF0, // C
        0xE0, 0x90, 0x90, 0x90, 0xE0, // D
        0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
        0xF0, 0x80, 0xF0, 0x80, 0x80  // F
    };
    memcpy(&chip8->ram[0], font, sizeof(font)); // load the font
    FILE *rom = fopen(rom_name, "rb");          // open ROM file
    if(!rom){
        SDL_Log("ROM file %s is invalid or does not exist\n", rom_name);
        return false;                           // failure
    }
    fseek(rom, 0, SEEK_END);                    // get/check ROM size
    const long rom_size = ftell(rom);
    const long max_size = sizeof chip8->ram - entry_point;
    rewind(rom);
    if(rom_size > max_size){
        SDL_Log("ROM file %s is too big! ROM size: %ld, Max size allowed: %ld\n",
                rom_name, rom_size, max_size);
        return false;                           // failure
    }
    if(fread(&chip8->ram[entry_point], rom_size, 1, rom)!=1){
        SDL_Log("Could not read ROM file %s into CHIP8 memory\n", rom_name);
        return false;                           // failure
    }
    fclose(rom);                                // close ROM file
    chip8->state = RUNNING;                     // Default machine state to on/running
    chip8->PC = entry_point;                    // Start program counter at ROM entry point
    chip8->rom_name = rom_name;                 // loadin ROM name
    chip8->stack_ptr = &chip8->stack[0];        // set stack pointer
    return true;                                // success
}

// Set up initial configs
bool set_config(config_t *config, int argc, char **argv){
    // Set defaults
    config->window_height = 32;     // CHIP8 original Y resolution
    config->window_width = 64;      // CHIP8 original X resolution
    config->fg_color = 0xFFFFFFFF;  // WHITE
    config->bg_color = 0x000000FF;  // BLACK
    config->scale_factor = 20;      // 20x scale factor 1280x640
    config->pixel_outlines = true;  // set pixel outlines as true by default
    config->clock_speed = 500;      // 500hz clock speed

    // Override defaults with command line arguments
    for(int i = 1;i<argc;i++){
        (void)argv[i];
    }
    return true;                        // success
}

//Final cleanup
void final_cleanup(const sdl_t sdl){
    SDL_DestroyRenderer(sdl.renderer);  //Destroy renderer
    SDL_DestroyWindow(sdl.window);      //Destroy window
    SDL_Quit();                         //Shut down SDL subsystem
}

//Initial screen clear to background color
void clear_screen(const config_t config,const sdl_t sdl){
    //Grab bg color values to draw outlines
    const uint8_t r = (config.bg_color >> 24) & 0xFF;
    const uint8_t g = (config.bg_color >> 16) & 0xFF;
    const uint8_t b = (config.bg_color >> 8) & 0xFF;
    const uint8_t a = (config.bg_color >> 0) & 0xFF;
    SDL_SetRenderDrawColor(sdl.renderer, r, g, b, a);
    SDL_RenderClear(sdl.renderer);
}

// Update window with any changes
void redraw_screen(const sdl_t sdl, const config_t config, chip8_t *chip8) {
    SDL_Rect rect = {.x=0, .y = 0, .w = config.scale_factor, .h = config.scale_factor};

    // Grab bg color values to draw outlines
    const uint8_t fg_r = (config.fg_color >> 24) & 0xFF;
    const uint8_t fg_g = (config.fg_color >> 16) & 0xFF;
    const uint8_t fg_b = (config.fg_color >>  8) & 0xFF;
    const uint8_t fg_a = (config.fg_color >>  0) & 0xFF;
    // Grab fg color vals to draw rectangles
    const uint8_t bg_r = (config.bg_color >> 24) & 0xFF;
    const uint8_t bg_g = (config.bg_color >> 16) & 0xFF;
    const uint8_t bg_b = (config.bg_color >>  8) & 0xFF;
    const uint8_t bg_a = (config.bg_color >>  0) & 0xFF;
    // loop and draw a rectangle per pixel to the window
    for (uint32_t i = 0; i < sizeof(chip8->display); i++){
        // translate 1D index i value to 2D X/Y coords
        // X = i % window width
        // Y = i / window width
        rect.x = (i % config.window_width) * config.scale_factor;
        rect.y = (i / config.window_width) * config.scale_factor;

        if (chip8->display[i]) {
            // Pixel is on, draw foreground color
            SDL_SetRenderDrawColor(sdl.renderer, fg_r, fg_g, fg_b, fg_a);
            SDL_RenderFillRect(sdl.renderer, &rect);
            // if user requested drawing pixel outlines draw those now
            if(config.pixel_outlines){
                SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
                SDL_RenderDrawRect(sdl.renderer, &rect);
            }

        }
        else{
            SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
            SDL_RenderFillRect(sdl.renderer, &rect);
        }
    }
    SDL_RenderPresent(sdl.renderer);
}

// handle user input
// CHIP8 Keypad     QWERTY
// 123C             1234
// 456D             qwer
// 789E             asdf
// A0BF             zxcv
void handle_input(chip8_t *chip8){
    SDL_Event event;
    while(SDL_PollEvent(&event)){
        switch(event.type){
            case SDL_QUIT:              //Exit window; End program
                chip8->state = QUIT;    //Will exit main emulator loop
                return;
            case SDL_KEYDOWN:
                switch(event.key.keysym.sym){
                    case SDLK_ESCAPE:   //Escape key; pause the execution
                        if(chip8->state == RUNNING){
                            chip8->state = PAUSED;
                            puts("===PAUSED===");
                        }
                        else{
                            chip8->state = RUNNING;
                            puts("===RUNNING===");
                        }
                        return;
                    // map qwerty keys to CHIP8 keypad
                    case SDLK_1: chip8->keypad[0x1] = true; break;
                    case SDLK_2: chip8->keypad[0x2] = true; break;
                    case SDLK_3: chip8->keypad[0x3] = true; break;
                    case SDLK_4: chip8->keypad[0xC] = true; break;

                    case SDLK_q: chip8->keypad[0x4] = true; break;
                    case SDLK_w: chip8->keypad[0x5] = true; break;
                    case SDLK_e: chip8->keypad[0x6] = true; break;
                    case SDLK_r: chip8->keypad[0xD] = true; break;

                    case SDLK_a: chip8->keypad[0x7] = true; break;
                    case SDLK_s: chip8->keypad[0x8] = true; break;
                    case SDLK_d: chip8->keypad[0x9] = true; break;
                    case SDLK_f: chip8->keypad[0xE] = true; break;

                    case SDLK_z: chip8->keypad[0xA] = true; break;
                    case SDLK_x: chip8->keypad[0x0] = true; break;
                    case SDLK_c: chip8->keypad[0xB] = true; break;
                    case SDLK_v: chip8->keypad[0xF] = true; break;
                    default: break;
                }
                break;
            case SDL_KEYUP:
                switch(event.key.keysym.sym){
                    // map qwerty keys to CHIP8 keypad
                    case SDLK_1: chip8->keypad[0x1] = false; break;
                    case SDLK_2: chip8->keypad[0x2] = false; break;
                    case SDLK_3: chip8->keypad[0x3] = false; break;
                    case SDLK_4: chip8->keypad[0xC] = false; break;

                    case SDLK_q: chip8->keypad[0x4] = false; break;
                    case SDLK_w: chip8->keypad[0x5] = false; break;
                    case SDLK_e: chip8->keypad[0x6] = false; break;
                    case SDLK_r: chip8->keypad[0xD] = false; break;

                    case SDLK_a: chip8->keypad[0x7] = false; break;
                    case SDLK_s: chip8->keypad[0x8] = false; break;
                    case SDLK_d: chip8->keypad[0x9] = false; break;
                    case SDLK_f: chip8->keypad[0xE] = false; break;

                    case SDLK_z: chip8->keypad[0xA] = false; break;
                    case SDLK_x: chip8->keypad[0x0] = false; break;
                    case SDLK_c: chip8->keypad[0xB] = false; break;
                    case SDLK_v: chip8->keypad[0xF] = false; break;
                    default: break;
                }
            default:
                break;
        }
    }
}

#ifdef DEBUG
void print_debug_info(chip8_t *chip8){
    //print debug info
    //decode opcode
    printf("Address: 0X%04X, Opcode: 0X%04X Description: ",
            chip8->PC-2, chip8->inst.opcode);
    switch ((chip8->inst.opcode >> 12) & 0x0F){
        case 0x0:
            if(chip8->inst.NN == 0xE0){
                //0x00E0: Clear the screen
                printf("Clear Screen\n");
            }
            else if(chip8->inst.NN == 0xEE){
                //0x00EE: Return from subroutine
                //Set program counter to last address on subroutine stack ("pop" it off the stack)
                printf("Return from subroutine to address 0X%04X\n", *(chip8->stack_ptr-1));
            }
            else{
                printf("Unimplemented Opcode.\n");
            }
            break;
        case 0x01:
            //0x1NNN: Jump to address NNN
            printf("Jump to address 0X%04X\n", chip8->inst.NNN);
            break;
        case 0x02:
            //0x2NNN: Call subroutine at NNN
            printf("Call subroutine at 0X%04X\n", chip8->inst.NNN);
            break;
        case 0x03:
            //0x3XNN: Check if VX == NN, if so, skip the next instruction
            printf("Check if V%X == 0X%02X\n", chip8->inst.X, chip8->inst.NN);
            if(chip8->V[chip8->inst.X] == chip8->inst.NN)
                printf("Skip next opcode\n"); //skip next opcode/instruction
            else printf("Do not skip next opcode\n");
            break;
        case 0x04:
            //0x4XNN: Check if VX != NN, if so, skip the next instruction
            printf("Check if V%X != 0X%02X\n", chip8->inst.X, chip8->inst.NN);
            if(chip8->V[chip8->inst.X] != chip8->inst.NN)
                printf("Skip next opcode\n"); //skip next opcode/instruction
            else printf("Do not skip next opcode\n");
            break;
        case 0x05:
            //0x5XY0: Check if VX == VY, if so, skip the next instruction
            printf("Check if V%X == V%X\n", chip8->inst.X, chip8->inst.Y);
            if(chip8->V[chip8->inst.X] == chip8->V[chip8->inst.Y])
                printf("Skip next opcode\n"); //skip next opcode/instruction
            else printf("Do not skip next opcode\n");
            break;
        case 0x06:
            //0x6XNN: Set register VX to NN
            printf("Set register V%X to 0X%02X\n", chip8->inst.X, chip8->inst.NN);
            break;
        case 0x07:
            //0x7XNN: Set register VX += NN
            printf("Set register V%X += 0X%02X\n", chip8->inst.X, chip8->inst.NN);
            break;
        case 0x08:
            //0x8XY0: Set register VX = VY
            if(chip8->inst.N == 0){
                printf("Set register V%X = V%X\n", chip8->inst.X, chip8->inst.Y);
                break;
            }
            //0x8XY1: Set register VX |= VY
            else if(chip8->inst.N == 1){
                printf("Set register V%X |= V%X\n", chip8->inst.X, chip8->inst.Y);
                break;
            }
            //0x8XY2: Set register VX &= VY
            else if(chip8->inst.N == 2){
                printf("Set register V%X &= V%X\n", chip8->inst.X, chip8->inst.Y);
                break;
            }
            //0x8XY3: Set register VX ^= VY
            else if(chip8->inst.N == 3){
                printf("Set register V%X ^= V%X\n", chip8->inst.X, chip8->inst.Y);
                break;
            }
            //0x8XY4: Set register VX += VY, set VF to 1 if carry
            else if(chip8->inst.N == 4){
                printf("Set register V%X += V%X, VF = 1 if carry; Result: 0X%02X, VF = %X\n",
                        chip8->inst.X, chip8->inst.Y,
                        chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y],
                        ((uint16_t)(chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y]) > 255));
                break;
            }
            //0x8XY5: Set register VX -= VY, set VF to 1 if there is not a borrow (result is positive/0)
            else if(chip8->inst.N == 5){
                printf("Set register V%X -= V%X, VF = 1 if no borrow; Result: 0X%02X, VF = %X\n",
                        chip8->inst.X, chip8->inst.Y,
                        chip8->V[chip8->inst.X] - chip8->V[chip8->inst.Y],
                        (chip8->V[chip8->inst.Y] <= chip8->V[chip8->inst.X]));
                break;
            }
            //0x8XY6: Set register VX >>= 1, store shifted off bit in VF
            else if(chip8->inst.N == 6){
                printf("Set register V%X >>= 1, store shifted off bit in VF\n", chip8->inst.X);
                break;
            }
            //0x8XY7: Set register VX = VY - VX, set VF to 1 if there is not a borrow (result is positive/0)
            else if(chip8->inst.N == 7){
                printf("Set register V%X = V%X - V%X, VF = 1 if no borrow; Result: 0X%02X, VF = %X\n",
                        chip8->inst.X, chip8->inst.Y, chip8->inst.X,
                        chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X],
                        (chip8->V[chip8->inst.X] <= chip8->V[chip8->inst.Y]));
                break;
            }
            //0x8XYE: Set register VX <<= 1, store shifted off bit in VF
            else if(chip8->inst.N == 0xE){
                printf("Set register V%X <<= 1, store shifted off bit in VF\n", chip8->inst.X);
                break;
            }
            else{
                printf("Unimplemented Opcode.\n");
                break; //unexpected opcode
            }
            break;
        case 0x09:
            //0x9XY0: Check if VX != VY; Skip next instruction if so
            printf("Check if V%X != V%X\n", chip8->inst.X, chip8->inst.Y);
            if(chip8->V[chip8->inst.X] != chip8->V[chip8->inst.Y])
                printf("Skip next opcode\n"); //skip next opcode/instruction
            else printf("Do not skip next opcode\n");
            break;
        case 0x0A:
            //0xANNN: Set index register to NNN
            printf("Set index register to 0X%04X\n", chip8->inst.NNN);
            break;
        case 0x0B:
            //0xBNNN: Jump to V0 + NNN
            printf("Jump to V0 + 0X%04X\n", chip8->inst.NNN);
            break;
        case 0x0C:
            //0xCXNN: Set VX = random number & NN
            printf("Set V%X = random %% 256 & 0x%02X\n", chip8->inst.X, chip8->inst.NN);
            break;
        case 0x0D:
            //0xDXYN: Draw N-height sprite at coords X,Y; Read from memory location I;
            //Set VF to 1 if any pixels are flipped from set to unset
            printf("Draw %u height sprite at coords V%X and V%X from memory location I. Set VF =1 if any pixels are turned off \n",
                chip8->inst.N, chip8->inst.X, chip8->inst.Y);
            break;
        case 0x0E:
            if(chip8->inst.NN == 0x9E){
                //skip next instruction if key stored in VX is pressed
                printf("Skip next instruction if key in V%X is pressed; Keypad val:%d \n",
                    chip8->V[chip8->inst.X], chip8->keypad[chip8->V[chip8->inst.X]]);
                if(chip8->keypad[(chip8->V[chip8->inst.X] & 0xF)])
                    printf("Skip next opcode\n"); //skip next opcode/instruction
                else printf("Do not skip next opcode\n");
            }
            else if (chip8->inst.NN == 0xA1) {
                //skip next instruction if key stored in VX is not pressed
                printf("Skip next instruction if key in V%X is not pressed; Keypad val:%d \n",
                    chip8->V[chip8->inst.X], chip8->keypad[chip8->V[chip8->inst.X]]);
                if(!chip8->keypad[(chip8->V[chip8->inst.X] & 0xF)])
                    printf("Skip next opcode\n"); //skip next opcode/instruction
                else printf("Do not skip next opcode\n");
            }
            break;
        case 0x0F:
            switch(chip8->inst.NN){
                case 0x07:
                    //set VX to the value of delay timer
                    printf("set V%X to the value of the %d \n",
                        chip8->V[chip8->inst.X], chip8->delay_timer);
                    break;

                case 0x0A:
                    // a key press is awaited so reset PC to temporarily stop exec
                    // blocks all instruction until key event and store key press in vx
                    printf("waiting for a key press the pressed key will be stored in V%X \n",
                        chip8->V[chip8->inst.X]);
                    break;

                case 0x15:
                    // set delay time to VX
                    printf("Set delay timer to V%X\n",
                        chip8->V[chip8->inst.X]);
                    break;

                case 0x18:
                    // set sound timer to VX
                    printf("set sound timer to V%X",
                    chip8->V[chip8->inst.X]);
                    break;

                case 0x1E:
                    // adds VX to I VF is not affected one game does depend on VF being affected
                    // and one game depends on VF not being affected so gotta choose one
                    // go for the recommended operation which is VF not affected
                    printf("Adds V%X to I",
                        chip8->V[chip8->inst.X]);
                    break;
                case 0x29:
                    // check for the character in chip8->V[chip8->inst.X] only the lowest nibble is considered
                    // store the sprite of that character in the Index register ie I reg
                    printf("Set I to the location of the sprite for the character in V%X\n",
                        chip8->V[chip8->inst.X]);
                    break;
                case 0x33:
                    // store the binary coded decimal of the value in chip8->V[chip8->inst.X] in memory starting from I
                    printf("Store the binary coded decimal of the value in V%X in memory starting from I\n",
                        chip8->V[chip8->inst.X]);
                    break;
                case 0x55:
                    // store the values of V0 to Vx in memory starting from I
                    printf("Store the values of V0 to V%X in memory starting from I\n",
                        chip8->inst.X);
                    break;
                case 0x65:
                    // fill the values of V0 to Vx with the values in memory starting from I
                    printf("Fill the values of V0 to V%X with the values in memory starting from I\n",
                        chip8->inst.X);
                    break;
                default :
                    break;
            }
            break;
        default:
            printf("Unimplemented Opcode.\n");
            break; //unexpected opcode

    }
}
#endif

//emulate CHIP8 instructions
void emulate_chip8(chip8_t *chip8 , config_t config){
    //fetch opcode from memory
    chip8->inst.opcode = chip8->ram[chip8->PC] << 8 | chip8->ram[chip8->PC+1];
    chip8->PC += 2; //increment program counter

    //handle the constants
    chip8->inst.NNN = chip8->inst.opcode & 0x0FFF;
    chip8->inst.NN = chip8->inst.opcode & 0x00FF;
    chip8->inst.N = chip8->inst.opcode & 0x000F;
    chip8->inst.X = (chip8->inst.opcode & 0x0F00) >> 8;
    chip8->inst.Y = (chip8->inst.opcode & 0x00F0) >> 4;

#ifdef DEBUG
    print_debug_info(chip8);
#endif

    //decode opcode
    switch ((chip8->inst.opcode >> 12) & 0x0F){
        case 0x0:
            if(chip8->inst.NN == 0xE0){
                //0x00E0: Clear the screen
                memset(&chip8->display[0], false, sizeof chip8->display);
                chip8->draw = true; //will update screen on next 60 hz tick
            }
            else if(chip8->inst.NN == 0xEE){
                //0x00EE: Return from subroutine
                //Set program counter to last address on subroutine stack ("pop" it off the stack)
                chip8->PC = *--chip8->stack_ptr;
            }
            else{
                // unimplemented opcode
            }
            break;
        case 0x01:
            //0x1NNN: Jump to address NNN
            chip8->PC = chip8->inst.NNN;                        //Set program counter so that next opcode is from NNN
            break;
        case 0x02:
            //0x2NNN: Call subroutine at NNN
            *chip8->stack_ptr++ = chip8->PC;                    // push current address to stack
            chip8->PC = chip8->inst.NNN;                        //set program counter to subroutine address
            break;
        case 0x03:
            //0x3XNN: Check if VX == NN, if so, skip the next instruction
            if(chip8->V[chip8->inst.X] == chip8->inst.NN)
                chip8->PC += 2; //skip next opcode/instruction
            break;
        case 0x04:
            //0x4XNN: Check if VX != NN, if so, skip the next instruction
            if(chip8->V[chip8->inst.X] != chip8->inst.NN)
                chip8->PC += 2; //skip next opcode/instruction
            break;
        case 0x05:
            //0x5XY0: Check if VX == VY, if so, skip the next instruction
            if(chip8->V[chip8->inst.X] == chip8->V[chip8->inst.Y])
                chip8->PC += 2; //skip next opcode/instruction
            break;
        case 0x06:
            //0x6XNN: Set register VX to NN
            chip8->V[chip8->inst.X] = chip8->inst.NN;
            break;
        case 0x07:
            //0x7XNN: Set register VX += NN
            chip8->V[chip8->inst.X] += chip8->inst.NN;
            break;
        case 0x08:
            switch(chip8->inst.N){
                case 0x0:
                    //0x8XY0: Set register VX = VY
                    chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y];
                    break;
                case 0x1:
                    //0x8XY1: Set register VX |= VY
                    chip8->V[chip8->inst.X] |= chip8->V[chip8->inst.Y];
                    break;
                case 0x2:
                    //0x8XY2: Set register VX &= VY
                    chip8->V[chip8->inst.X] &= chip8->V[chip8->inst.Y];
                    break;
                case 0x3:
                    //0x8XY3: Set register VX ^= VY
                    chip8->V[chip8->inst.X] ^= chip8->V[chip8->inst.Y];
                    break;
                case 0x4:
                    //0x8XY4: Set register VX += VY, set VF to 1 if carry
                    chip8->V[0xF] = (chip8->V[chip8->inst.Y] > (0xFF - chip8->V[chip8->inst.X]));
                    chip8->V[chip8->inst.X] += chip8->V[chip8->inst.Y];
                    break;
                case 0x5:
                    //0x8XY5: Set register VX -= VY, set VF to 1 if there is not a borrow (result is positive/0)
                    chip8->V[0xF] = (chip8->V[chip8->inst.Y] <= chip8->V[chip8->inst.X]);
                    chip8->V[chip8->inst.X] -= chip8->V[chip8->inst.Y];
                    break;
                case 0x6:
                    //0x8XY6: Set register VX >>= 1, store shifted off bit in VF
                    chip8->V[0xF] = chip8->V[chip8->inst.X] & 1;
                    chip8->V[chip8->inst.X] >>= 1;
                    break;
                case 0x7:
                    //0x8XY7: Set register VX = VY - VX, set VF to 1 if there is not a borrow (result is positive/0)
                    chip8->V[0xF] = (chip8->V[chip8->inst.X] <= chip8->V[chip8->inst.Y]);
                    chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X];
                    break;
                case 0xE:
                    //0x8XYE: Set register VX <<= 1, store shifted off bit in VF
                    chip8->V[0xF] = (chip8->V[chip8->inst.X] & 0x80) >> 7;
                    chip8->V[chip8->inst.X] <<= 1;
                    break;
                default:
                    //handle unexpected N val
                    break;
            }

            break;

        case 0x09:
            //0x9XY0: Check if VX != VY; Skip next instruction if so
            if(chip8->V[chip8->inst.X] != chip8->V[chip8->inst.Y])
                chip8->PC += 2;
            break;

        case 0x0A:
            //0xANNN: Set index register to NNN
            chip8->I = chip8->inst.NNN;
            break;

        case 0x0B:
            //0xBNNN: Jump to V0 + NNN
            chip8->PC = chip8->V[0] + chip8->inst.NNN;
            break;

        case 0x0C:
            //0xCXNN: Set register VX to random number between 0 and 255 and do AND operation with NN
            chip8->V[chip8->inst.X] = (rand() % 256) & chip8->inst.NN;
            break;

        case 0x0D: {
            //0xDXYN: Draw N-height sprite at coords X,Y; Read from memory location I;
            //Set VF to 1 if any pixels are flipped from set to unset
            uint8_t X_coord = chip8->V[chip8->inst.X] % config.window_width;
            uint8_t Y_coord = chip8->V[chip8->inst.Y] % config.window_height;
            const uint8_t org_X = X_coord; // save the initial value of x coordinate

            chip8->V[0xF] = 0; //initailize carry to 1
            for (uint8_t i = 0; i < chip8->inst.N; i++){
                // set the sprite data starting from the address in the I register
                const uint8_t sprite_data = chip8->ram[chip8->I+i];
                X_coord = org_X;

                for (int8_t j = 7; j>=0; j--){
                    bool *pixel = &chip8->display[Y_coord * config.window_width + X_coord];
                    const bool sprite_bit = (sprite_data & (1<<j));
                    if(sprite_bit && *pixel){
                        chip8->V[0xF] = 1;
                    }
                    *pixel ^= sprite_bit;
                    if(++X_coord >=config.window_width) break;
                }
                if(++Y_coord >=config.window_height) break;
            }
            chip8->draw = true;
            break;
        }
        case 0x0E:
            if(chip8->inst.NN == 0x9E){
                //skip next instruction if key stored in VX is pressed
                if(chip8->keypad[(chip8->V[chip8->inst.X] & 0x0F)]){ //avoid seg faults by extracting the last nibble
                    chip8->PC += 2;
                }
            }
            else if (chip8->inst.NN == 0xA1) {
                //skip next instruction if key stored in VX is not pressed
                if(!chip8->keypad[(chip8->V[chip8->inst.X] & 0x0F)]){ //avoid seg faults by extracting the last nibble
                    chip8->PC += 2;
                }
            }
            break;

        case 0x0F:
            switch(chip8->inst.NN){
                case 0x07:
                    //set VX to the value of delay timer
                    chip8->V[chip8->inst.X] = chip8->delay_timer;
                    break;

                case 0x0A:
                    // a key press is awaited so reset PC to temporarily stop exec
                    // blocks all instruction until key event and store key press in vx
                    bool key_pressed = true;
                    for(uint8_t i = 0; i < sizeof chip8->keypad; i++){
                        if(chip8->keypad[i]){
                            chip8->V[chip8->inst.X] = i;
                            key_pressed = true;
                            break;
                        }
                    }
                    if (!key_pressed){
                        chip8->PC -= 2;
                    }
                    break;

                case 0x15:
                    // set delay time to VX
                    chip8->delay_timer = chip8->V[chip8->inst.X];
                    break;

                case 0x18:
                    // set sound timer to VX
                    chip8->sound_timer = chip8->V[chip8->inst.X];
                    break;

                case 0x1E:
                    // adds VX to I VF is not affected one game does depend on VF being affected
                    // and one game depends on VF not being affected so gotta choose one
                    // go for the recommended operation which is VF not affected
                    chip8->I += chip8->V[chip8->inst.X];
                    break;

                case 0x29:
                    // check for the character in chip8->V[chip8->inst.X] only the lowest nibble is considered
                    // store the sprite of that character in the Index register ie I reg
                    uint8_t character = chip8->V[chip8->inst.X] & 0x0F;
                    chip8->I = (character*5);
                    break;
                case 0x33:
                    // store the binary coded decimal of the value in chip8->V[chip8->inst.X] in memory starting from I
                    uint8_t value = chip8->V[chip8->inst.X];
                    chip8->ram[chip8->I+2] = value % 10;
                    value /= 10;
                    chip8->ram[chip8->I+1] = value % 10;
                    value /= 10;
                    chip8->ram[chip8->I] = value;
                    break;
                case 0x55:
                    // store the values of V0 to Vx in memory starting from I
                    // Schip does not increment I register
                    for(uint8_t i = 0; i <= chip8->inst.X; i++){
                        chip8->ram[chip8->I + i] = chip8->V[i];
                    }
                    break;
                case 0x65:
                    // load the values of V0 to Vx with the values in memory starting from I
                    for(uint8_t i = 0; i <= chip8->inst.X; i++){
                        chip8->V[i] = chip8->ram[chip8->I + i];
                    }
                    break;
                default:
                    break;
            }
        default:
            break; //unexpected opcode
    }
}

//update the timers
void update_timers(chip8_t *chip8){
    if(chip8->delay_timer > 0) chip8->delay_timer--;
    if(chip8->sound_timer > 0) chip8->sound_timer--;
    // play sound if sound timer is greater than 0
    // else stop the sound
}

//main sequence
int main(int argc, char **argv){
    //Default usage message for args
    if(argc < 2){
        fprintf(stderr, "Usage: %s <rom_name>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    //initialize configuration
    config_t config = {0};
    if(!set_config(&config, argc, argv)) exit(EXIT_FAILURE);

    //Initialize SDL
    sdl_t sdl = {0};
    if(!init_sdl(&sdl, config)) exit(EXIT_FAILURE);

    //Initialize CHIP8 machine
    chip8_t chip8 = {0};
    const char *rom_name = argv[1];
    if(!init_chip8(&chip8, rom_name)) exit(EXIT_FAILURE);

    //Initial screen clear to background color
    clear_screen(config, sdl);

    srand(time(NULL));

    //Main emulator loop
    while (chip8.state != QUIT){
        //Handle user input
        handle_input(&chip8);
        if(chip8.state == PAUSED) continue;
        //get_time
        const uint64_t before = SDL_GetPerformanceCounter();
        //Emulate CHIP8 Instructions for this emulator "frame" (60hz)
        for (uint8_t i = 0; i < config.clock_speed/60; i++)
            emulate_chip8(&chip8, config);

        //get_time elapsed since last get_time
        const uint64_t after = SDL_GetPerformanceCounter();

        //Delay for 60 hz
        const double elapsed = (double)((after - before) / 1000) / SDL_GetPerformanceFrequency();

        //SDL_Delay(1000/60-actual time elapsed)
        SDL_Delay(16.67f > elapsed ? 16.67f - elapsed : 0);

        //update window with changes every 60hz
        redraw_screen(sdl , config , &chip8);

        // update the timers
        update_timers(&chip8);
    }

    //Final cleanup
    final_cleanup(sdl);
    exit(EXIT_SUCCESS);
    return 0;
}
