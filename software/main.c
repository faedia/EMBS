#include <stdio.h>
#include "xparameters.h"
#include "platform.h"
#include "xil_printf.h"
#include "xil_cache.h"
#include "zybo_z7_hdmi/display_ctrl.h"
#include "xtoplevel.h"
#include "xuartps_hw.h"

// Frame size (based on 1440x900 resolution, 32 bits per pixel)
#define MAX_FRAME (1440*900)
#define FRAME_STRIDE (1440*4)
// The header of the response from the server
#define RESP_HEADER 0x02
// Maximum size of a puzzle
#define MAX_SIZE 10
// Maximum size of the buffer for storing solutions
#define MAX_BUF_SIZE 20
// Number of hardware solvers
#define SOLVER_COUNT 4

#define RED_CODE 0x00
#define GREEN_CODE 0x01
#define BLUE_CODE 0x02
#define YELLOW_CODE 0x03
#define MAGENTA_CODE 0x04
#define CYAN_CODE 0x05
#define WHITE_CODE 0x06
#define PURPLE_CODE 0x07
#define ORANGE_CODE 0x08
#define LIME_CODE 0x09
// Generates the pixel values for the colours
#define RED     (0xFF << BIT_DISPLAY_RED) | (0x00 << BIT_DISPLAY_GREEN) | (0x00 << BIT_DISPLAY_BLUE)
#define GREEN   (0x00 << BIT_DISPLAY_RED) | (0xFF << BIT_DISPLAY_GREEN) | (0x00 << BIT_DISPLAY_BLUE)
#define BLUE    (0x00 << BIT_DISPLAY_RED) | (0x00 << BIT_DISPLAY_GREEN) | (0xFF << BIT_DISPLAY_BLUE)
#define YELLOW  (0xFF << BIT_DISPLAY_RED) | (0xFF << BIT_DISPLAY_GREEN) | (0x00 << BIT_DISPLAY_BLUE)
#define MAGENTA (0xFF << BIT_DISPLAY_RED) | (0x00 << BIT_DISPLAY_GREEN) | (0xFF << BIT_DISPLAY_BLUE)
#define CYAN    (0x00 << BIT_DISPLAY_RED) | (0xFF << BIT_DISPLAY_GREEN) | (0xFF << BIT_DISPLAY_BLUE)
#define WHITE   (0xFF << BIT_DISPLAY_RED) | (0xFF << BIT_DISPLAY_GREEN) | (0xFF << BIT_DISPLAY_BLUE)
#define PURPLE  (0x63 << BIT_DISPLAY_RED) | (0x00 << BIT_DISPLAY_GREEN) | (0x99 << BIT_DISPLAY_BLUE)
#define ORANGE  (0xFF << BIT_DISPLAY_RED) | (0x94 << BIT_DISPLAY_GREEN) | (0x00 << BIT_DISPLAY_BLUE)
#define LIME    (0x53 << BIT_DISPLAY_RED) | (0x56 << BIT_DISPLAY_GREEN) | (0x1B << BIT_DISPLAY_BLUE)

// Makes a tile with the given colours
#define MAKE_TILE(c1, c2, c3, c4) ((c1 << 24) | (c2 <<16) | (c3 << 8) | (c4))

// The statemachine for the software
// This is so the main loop knows what to do
#define GET_SIZE 0
#define GET_SEED 1
#define RUN_PUZZLE 2
#define RUNNING 3

// Struct to send the data to the server to request the puzzle
// seed is 4 bytes but is represented as a byte array so that the alignment of the struct is 1 byte
typedef struct {
    u8 req_header;
    u8 size;
    u8 seed[4];
} req_t;

// Struct for the tiles
typedef struct {
    u8 top;
    u8 bottom;
    u8 left;
    u8 right;
} tile_t;

// Struct to store a recieved puzzle
typedef struct {
    u8 size;
    tile_t tiles[MAX_SIZE * MAX_SIZE];
} puzzle_t;

tile_t make_tile(u32 data);
puzzle_t make_puzzle(u8 size, u32 *data);
void udp_get_handler(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port);
void request_puzzle(u8 size, u32 seed);
void display_tile(tile_t* tile, u8 size, u32 idx);
u32 get_color(u8 color);
void display_puzzle(tile_t* puzzle, uint32_t size);
void init_hdmi();
void print_puzzle(tile_t *tiles, uint32_t size);
uint8_t traverse_puzzles(char byte);
u8 puzzle_eq(tile_t *p1, tile_t *p2, u8 size);
u8 is_sol_unique(tile_t *p);
u8 all_done(u8 *arr);
void solve_puzzle();

// Global object for the current puzzle being solved or has just been solved
puzzle_t current_puzzle;
// Array of tiles to be used as memory for the hardware solvers
tile_t tiles[SOLVER_COUNT][MAX_SIZE * MAX_SIZE];

// The buffer for solutions to display
uint32_t sol_buf[MAX_BUF_SIZE][MAX_SIZE * MAX_SIZE];
// How many solutions are currently in the buffer
int32_t sol_buf_size;
// Current solution being displayed, note if -1 then the original puzzle layout from the server is displayed
int32_t sol_buf_idx;

// Array of hardware solvers
XToplevel hls[SOLVER_COUNT];

DisplayCtrl dispCtrl; // Display driver struct
u32 frameBuf[DISPLAY_NUM_FRAMES][MAX_FRAME]; // Frame buffers for video data
void *pFrames[DISPLAY_NUM_FRAMES]; // Array of pointers to the frame buffers

// State so that the software knows if it needs to get the size or seed or if it is solving a puzzle
u8 state;

// Char buffer to store data from the serial
char char_buffer[255];
u32 char_buffer_idx = 0;

// the input size and seed to be requested
u32 input_size;
u32 seed;

// Global object for requesting puzzles from the server
req_t req;

tile_t make_tile(u32 data)
{

    tile_t tile = {
		// Top
		((u8 *)(&data))[0],
		// Bottom
        ((u8 *)(&data))[2],
		// Left
        ((u8 *)(&data))[3],
		// Right
        ((u8 *)(&data))[1]
    };
    return tile;
}

puzzle_t make_puzzle(u8 size, u32 *data)
{
    puzzle_t puzzle;
    puzzle.size = size;
    // For all tiles make the tile and store it
    for (int i = 0; i < size * size; i++)
        puzzle.tiles[i] = make_tile(data[i]);
    return puzzle;
}

void udp_get_handler(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    if(p) {
        u8 *data = p->payload;
        switch (data[0])
        {
            case RESP_HEADER:
            {
                u8 size = data[1];
                // Output seed and size information
                xil_printf("size: %u, seed: %u\r\n", size, *((u32 *)(data + 2)));
                // Make the puzzle and display, then set the state to run the puzzle
                current_puzzle = make_puzzle(size, (u32 *)(data + 6));
                display_puzzle(current_puzzle.tiles, size);
                state = RUNNING;
                break;
            }
        
            default:
            	xil_printf("unknown: %s\r\n", data);
            	break;
        }

        // Free the buffer when no longer needed
        pbuf_free(p);
    }
}
 
void request_puzzle(u8 size, u32 seed)
{
	// Log the information about the request
    xil_printf("Requesting puzzle of size %u with seed %u.\r\n", size, seed);
    struct udp_pcb *send_pcb = udp_new();
    // Build the payload to send
    req.req_header = 0x01;
    req.size = size;
    *((u32 *)req.seed) = seed;

    // Create the buffer and assign the payload and lengths
    struct pbuf *buf = pbuf_alloc(PBUF_TRANSPORT, 6, PBUF_REF);
    buf->payload = &req;
    buf->len = 6;
    // Send the data to the server
    ip_addr_t ip;
    IP4_ADDR(&ip, 192, 168, 10, 1);
    err_t err = udp_sendto(send_pcb, buf, &ip, 51050);
    switch (err)
    {
        case ERR_OK:
            break;
        default:
            xil_printf("Network Error\r\n");
            break;
    }


    // Free the network information when done
    pbuf_free(buf);
    udp_remove(send_pcb);
}

void display_tile(tile_t* tile, u8 size, u32 idx)
{
    // Get parameters from display controller struct
	int init_x, init_y;
	u32 stride = dispCtrl.stride / 4;
	u32 height = dispCtrl.vMode.height;
	u32 *frame = (u32 *)dispCtrl.framePtr[dispCtrl.curFrame];

	u32 tile_size = (height / size) - 5;

    init_x = (idx % size) * (height / size) + 5;
    init_y = (idx / size) * (height / size) + 5;

    int x = init_x;
    int y = init_y;

    // Display top triabgle
    for (int height = 0; height < (tile_size / 2); height++)
    {
        for (int width = 0; width < (tile_size - (height * 2)); width++)
        {
            frame[(y+height)*stride+(x+height+width)] = get_color(tile->top);
        }
    }


    // Display right triangle
    for (int width = 0; width < (tile_size / 2); width++)
    {
        for (int height = 0; height < (tile_size - (width * 2)); height++)
        {
            frame[(y+height+width)*stride+(x+(tile_size-width))] = get_color(tile->right);
        }
    }


    // Display bottom triangle
    for (int height = 0; height < (tile_size / 2); height++)
    {
        for (int width = 0; width < (tile_size - (height * 2)); width++)
        {
            frame[(y+(tile_size-height - 1))*stride+(x+height+width)] = get_color(tile->bottom);
        }
    }


    // Display left triangle
    for (int width = 0; width < (tile_size / 2); width++)
    {
        for (int height = 0; height < (tile_size - (width * 2)); height++)
        {
            frame[(y+height+width)*stride+(x+width)] = get_color(tile->left);
        }
    }

}

u32 get_color(u8 color)
{
	// This switch statement gets the pixel colour information from the colour information from the server
    switch (color)
    {
        case RED_CODE:
            return RED;
        case GREEN_CODE:
            return GREEN;
        case BLUE_CODE:
            return BLUE;
        case YELLOW_CODE:
            return YELLOW;
        case MAGENTA_CODE:
            return MAGENTA;
        case CYAN_CODE:
            return CYAN;
        case WHITE_CODE:
            return WHITE;
        case PURPLE_CODE:
            return PURPLE;
        case ORANGE_CODE:
            return ORANGE;
        case LIME_CODE:
            return LIME;
        default:
            return 0;
    }
}

void display_puzzle(tile_t* puzzle, uint32_t size)
{
	int x, y;
	u32 stride = dispCtrl.stride / 4;
	u32 width = dispCtrl.vMode.width;
	u32 height = dispCtrl.vMode.height;
	u32 *frame = (u32 *)dispCtrl.framePtr[dispCtrl.curFrame];

	// Fill the screen with a grey background
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			frame[y*stride + x] = (0x44 << BIT_DISPLAY_RED) | (0x44 << BIT_DISPLAY_GREEN) | (0x44 << BIT_DISPLAY_BLUE);
		}
	}


	// Display all of the tiles
	for (int i = 0; i < size * size; i++)
	{
		display_tile(puzzle + i, size, i);
	}

	// Flush the video buffer
    Xil_DCacheFlush();
}

void init_hdmi()
{
    // Initialise an array of pointers to the 2 frame buffers
	int i;
	for (i = 0; i < DISPLAY_NUM_FRAMES; i++)
		pFrames[i] = frameBuf[i];

    // Initialise the display controller
	DisplayInitialize(&dispCtrl, XPAR_AXIVDMA_0_DEVICE_ID, XPAR_VTC_0_DEVICE_ID, XPAR_HDMI_AXI_DYNCLK_0_BASEADDR, pFrames, FRAME_STRIDE);

	// Use first frame buffer (of two)
	DisplayChangeFrame(&dispCtrl, 0);

	// Set the display resolution
	DisplaySetMode(&dispCtrl, &VMODE_1440x900);

	// Enable video output
	DisplayStart(&dispCtrl);

	printf("\n\r");
	printf("HDMI output enabled\n\r");
	printf("Current Resolution: %s\n\r", dispCtrl.vMode.label);
	printf("Pixel Clock Frequency: %.3fMHz\n\r", dispCtrl.pxlFreq);
	printf("Drawing gradient pattern to screen...\n\r");

	// Get parameters from display controller struct
	int x, y;
	u32 stride = dispCtrl.stride / 4;
	u32 width = dispCtrl.vMode.width;
	u32 height = dispCtrl.vMode.height;
	u32 *frame = (u32 *)dispCtrl.framePtr[dispCtrl.curFrame];
	u32 red, green, blue;

	// Fill the screen with a nice gradient pattern
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			green = (x*0xFF) / width;
			blue = 0xFF - ((x*0xFF) / width);
			red = (y*0xFF) / height;
			frame[y*stride + x] = (red << BIT_DISPLAY_RED) | (green << BIT_DISPLAY_GREEN) | (blue << BIT_DISPLAY_BLUE);
		}
	}

	// Flush the cache, so the Video DMA core can pick up our frame buffer changes.
	// Flushing the entire cache (rather than a subset of cache lines) makes sense as our buffer is so big
	Xil_DCacheFlush();

	printf("Done.\n\r");
}

void print_puzzle(tile_t *tiles, uint32_t size)
{
	// Print all the puzzle information, useful for when debugging with out HDMI
	xil_printf("Puzzle: ");
	for (int i = 0; i < (size * size); i++)
	{
		xil_printf(":%u,%u,%u,%u",tiles[i].top,tiles[i].bottom,tiles[i].left,tiles[i].right);
	}
	xil_printf("\r\n");
}

uint8_t traverse_puzzles(char byte)
{
	// Traverse through the solutions when getting serial input
	// This also returns whether or not the solvers should abort
	if (byte == 'a')
	{
		char_buffer_idx = 0;
		char_buffer[0] = '\0';
		return 1;
	}
	else if (byte == 'p')
	{
		if (sol_buf_idx >= 0)
		{
			sol_buf_idx--;
			if (sol_buf_idx == -1)
				display_puzzle(current_puzzle.tiles, current_puzzle.size);
			else
				display_puzzle((tile_t *)sol_buf[sol_buf_idx], current_puzzle.size);
		}
	} else if (byte == 'n')
	{
		if (sol_buf_idx < (sol_buf_size - 1))
		{
			display_puzzle((tile_t *)sol_buf[++sol_buf_idx], current_puzzle.size);
		}
	}

	return 0;
}

u8 puzzle_eq(tile_t *p1, tile_t *p2, u8 size)
{
	// Check if 2 solutions are equal
	for (u8 i = 0; i < (size * size); i++)
	{
		if (	p1[i].top != p2[i].top ||
				p1[i].bottom != p2[i].bottom||
				p1[i].left != p2[i].left ||
				p1[i].right != p2[i].right)
			return 0;
	}

	return 1;
}

u8 is_sol_unique(tile_t *p)
{
	// Check to see if a solution is unique in the buffer of solutions
	for (int8_t i = 0; i < sol_buf_size; i++)
	{
		if (puzzle_eq(p, (tile_t *)sol_buf[i], current_puzzle.size))
			return 0;
	}
	return 1;
}

u8 all_done(u8 *arr)
{
	// Return whether all of the solvers have completed
	for (int i = 0; i < SOLVER_COUNT; i++)
	{
		if (arr[i] == 0)
			return 0;
	}

	return 1;
}

void solve_puzzle()
{
	// Reset the solution buffer information
	sol_buf_size = 0;
	sol_buf_idx = 0;

	// Default abort to 0;
	int aborted = 0;
	for (int i = 0; i < SOLVER_COUNT; i++)
	{
		// Make sure the solver is initialised and the ram is set correctly
		XToplevel_Initialize(&hls[i], i);
		XToplevel_Set_ram(&hls[i], (int)tiles[i]);

		// Define the start and end index's for this solver
		int start = (i * (current_puzzle.size * current_puzzle.size)) / (SOLVER_COUNT);
		int end = ((i + 1) * (current_puzzle.size * current_puzzle.size)) / (SOLVER_COUNT);

		// Copy the tiles to the ram
		memcpy(tiles[i], current_puzzle.tiles, MAX_SIZE * MAX_SIZE * sizeof(tile_t));

		// Flush the ram
		Xil_DCacheFlush();

		// Set the input parameters, specifically make sure the solver is reset
		XToplevel_Set_reset(&hls[i], 1);
		XToplevel_Set_in_size(&hls[i], current_puzzle.size);
		XToplevel_Set_in_start_idx(&hls[i], start);
		XToplevel_Set_in_end_idx(&hls[i], end);
		XToplevel_Set_abort(&hls[i], aborted);
	}

	print_puzzle(current_puzzle.tiles, current_puzzle.size);

	// Start all of the solvers
	for (int i = 0; i < SOLVER_COUNT; i++)
		XToplevel_Start(&hls[i]);

	// Set the done array to 0 for all of the solvers
	u8 done[SOLVER_COUNT] = {0, 0, 0, 0};

	// While there is still a solver running perform this loop
	while (!all_done(done))
	{

		// Handle the ethernet so we don't run out of pbuf's
		handle_ethernet();

		// If there is data on the serial consume it and perform the relevant action
		if (XUartPs_IsReceiveData(STDIN_BASEADDRESS))
		{
			char byte = XUartPs_RecvByte(STDIN_BASEADDRESS);
			aborted |= traverse_puzzles(byte);
		}

		// Check each solver
		for (int i = 0; i < SOLVER_COUNT; i++)
		{
			// If the solver is done the mark it as done
			if (XToplevel_IsDone(&hls[i]))
			{
				done[i] = 1;
				// If the return value says it has not finished the search space and it's found a solution then...
				if (XToplevel_Get_return(&hls[i]) && sol_buf_size != MAX_BUF_SIZE)
				{
					// Invalidate the cache so that the solution is in memory
					Xil_DCacheInvalidateRange((int)tiles[i], MAX_SIZE * MAX_SIZE * sizeof(tile_t));
					// If the solution is unique then add the solution to the solution buffer
					if (is_sol_unique(tiles[i]))
					{
						xil_printf("Found solution: %u\r\n", sol_buf_size + 1);
						memcpy(sol_buf[sol_buf_size], tiles[i], MAX_SIZE * MAX_SIZE * sizeof(uint32_t));
						print_puzzle((tile_t *)sol_buf[sol_buf_size], current_puzzle.size);
						sol_buf_size++;
						// If it is the first solution the display it
						// If it is the last solution then abort the hardware solvers
						if (sol_buf_size == 1)
						{
							display_puzzle((tile_t *)sol_buf[0], current_puzzle.size);
						}
						else if (sol_buf_size == MAX_BUF_SIZE)
						{
							aborted = 1;
						}
					}
					else
					{
						//xil_printf("Not unique\r\n");
					}
					// If have not aborted and we know we have search space left then restart the solver and mark it as not done
					if (!aborted)
					{
						XToplevel_Set_reset(&hls[i], 0);
						XToplevel_Start(&hls[i]);
						done[i] = 0;
					}
				}
			}

			// On each loop set the abort line to the aborted variable
			XToplevel_Set_abort(&hls[i], aborted);
		}
	}

	// Change the state so that the user can request another puzzle
	xil_printf("Execution completed!\r\n");
	state = GET_SIZE;
	xil_printf("size: ");
}

int main()
{
    init_hdmi();

    ip_addr_t ipaddr, netmask;
    unsigned char mac_ethernet_address[] = {0x00, 0x11, 0x22, 0x33, 0x00, 0x59}; // Put your own MAC address here!

    init_platform(mac_ethernet_address, &ipaddr, &netmask);

    // Initialise all the solvers
    for (int i = 0; i < SOLVER_COUNT; i++)
    {
        XToplevel_Initialize(&hls[i], i);
        XToplevel_Set_ram(&hls[i], (int)tiles[i]);
    }

    // Setup the listner so that we can recieve responses from the server
    struct udp_pcb *recv_pcb = udp_new();
    if (!recv_pcb)
        xil_printf("Error couldn't create recv pcb!");

    // Setup listener
    udp_bind(recv_pcb, IP_ADDR_ANY, 51050);
    udp_recv(recv_pcb, udp_get_handler, NULL);

    //Now enter the handling loop
    xil_printf("size: ");

    while(1) {
    	// If we have data in the serial the consume it and perform the relevant action
        if (XUartPs_IsReceiveData(STDIN_BASEADDRESS))
        {
            char byte = XUartPs_RecvByte(STDIN_BASEADDRESS);
            // If return has been pressed then we want to handle the input buffer as required
            if (byte == '\r')
            {
                xil_printf("\r\n");
                switch (state)
                {
                    case GET_SIZE:
                    	// Get the current input size and if it is valid then move state otherwise reset
                    	input_size = atoi(char_buffer);
                        if (input_size > 1 && input_size < 11)
                        {
                            char_buffer_idx = 0;
                            char_buffer[0] = '\0';
                            state = GET_SEED;
                            xil_printf("seed: ");
                        }
                        else
                        {
                            xil_printf("size: ");
                            char_buffer_idx = 0;
                            char_buffer[0] = '\0';
                        }
                        break;
                    case GET_SEED:
                    	// If there is something in the char buffer then set the seed to that otherwise the seed is 0
                        if (char_buffer_idx == 0)
                            seed = 0;
                        else
                            seed = atoi(char_buffer);
                        // We need to convert the seed to little endian
                        uint32_t seed_tmp;
                        ((uint8_t *)(&seed_tmp))[0] = ((uint8_t *)(&seed))[3];
                        ((uint8_t *)(&seed_tmp))[1] = ((uint8_t *)(&seed))[2];
                        ((uint8_t *)(&seed_tmp))[2] = ((uint8_t *)(&seed))[1];
                        ((uint8_t *)(&seed_tmp))[3] = ((uint8_t *)(&seed))[0];
                        seed = seed_tmp;

                        // Reset the char buffer and set the state so that we are expecting to recieve a puzzle
                        char_buffer_idx = 0;
                        char_buffer[0] = '\0';
                        state = RUN_PUZZLE;
                        // Request a puzzle with the given parameters from the server
                        current_puzzle.size = input_size;
                        request_puzzle(current_puzzle.size, seed);
                        break;
                    default:
                        break;
                }
            }
            else
            {
            	// If the input is not a number then we want to traverse the current solutions
            	// Otherwise we add the digit to the char buffer
                if (byte < '0' || byte > '9')
                {
                	traverse_puzzles(byte);
                }
                else
                {
                    char_buffer[char_buffer_idx++] = byte;
                	char_buffer[char_buffer_idx] = '\0';
                    outbyte(byte);
                }
            }
            
        }

        // If we are in the state to run the puzzle then run it
        if (state == RUNNING)
        {
        	solve_puzzle();
        }

        // We always want to handle ethernet if we can!
        handle_ethernet();
    }
    return 0;
}
