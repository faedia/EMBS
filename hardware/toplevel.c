#include "toplevel.h"
#include <string.h>
#define MAX_TILES MAX_SIZE * MAX_SIZE

// These defines are used get the colour values for each segment of the tile
#define TOP(x) 		(((uint8 *)(x))[0])
#define BOTTOM(x) 	(((uint8 *)(x))[1])
#define LEFT(x) 	(((uint8 *)(x))[2])
#define RIGHT(x) 	(((uint8 *)(x))[3])

// This struct is used to store the information that we require for back tracking
// We need to know the index of the tile at the current position
// We also need to know the rotation of that tile in the event of a back track
typedef struct stack_item_s
{
	uint8 idx;
	uint3 rot;
} stack_item_t;


void dec_current();
void inc_current();
void backtrack();
uint1 get_tile();
uint1 valid_x(uint8 idx);
uint1 check_tile(uint8 idx, uint32 tile, uint3 init_rot);
uint32 clockwise_rotate(uint32 tile);

//This is the list of tiles given to the system from the memory
uint32 tiles[MAX_TILES];
// This marks whether the tile at the current index is being used in the current solution
uint1 used[MAX_TILES];
// This holds the current solution
uint32 current_grid[MAX_TILES];
// The stack so that we can perform back tracking
// The stack is an array of stack items where the index is equavilent to the index in current_grid
// Therefore each part of this array stores information about currently filled in tiles
// It also gives us enough information about previous tiles used in the that position in the current tile configuration
stack_item_t stack[MAX_TILES];

// The current index in the current_grid and stack array
uint8 current_idx;
// This is the x and y values for the corresponding current_idx
// Storing these means we no longer require to perform division or module to figure them out
// This saves both space and computation time
uint4 current_y;
uint4 current_x;

// Size of the puzzle and total tiles in puzzle
uint4 size;
uint8 total_size;

// This defines the whole search space this IP core is going to run in
// It starts the first itme in the grid with the tile in ram at the given start index
uint8 start_idx;
// Same as above but for end index
uint8 end_idx;

uint1 toplevel(uint32 *ram, uint1 *reset, uint4 *in_size, uint8 *in_start_idx, uint8 *in_end_idx, uint1 *abort)
{
	#pragma HLS INTERFACE m_axi port=ram offset=slave bundle=MAXI
	#pragma HLS INTERFACE s_axilite port=reset bundle=AXILiteS register
	#pragma HLS INTERFACE s_axilite port=in_size bundle=AXILiteS register
	#pragma HLS INTERFACE s_axilite port=in_start_idx bundle=AXILiteS register
	#pragma HLS INTERFACE s_axilite port=in_end_idx bundle=AXILiteS register
	#pragma HLS INTERFACE s_axilite port=abort bundle=AXILiteS register
	#pragma HLS INTERFACE s_axilite port=return bundle=AXILiteS register

	// If the reset pin is high then we want to rest the data
	// This means we only reset when required making it trivial to get multiple solutions from a single IP core
	if (*reset)
	{
		memcpy(&tiles, ram, MAX_TILES * sizeof(uint32));
		memset(&used, 0, MAX_TILES * sizeof(uint1));
		//memset(&current_grid, 0, MAX_TILES * sizeof(uint32));
		for (uint8 i = 0; i < MAX_TILES; i++)
		{
			stack[i].idx = 0;
			stack[i].rot = 0;
		}

		start_idx = *in_start_idx;
		end_idx = *in_end_idx;

		stack[0].idx = start_idx;

		current_idx = 0;
		current_y = 0;
		current_x = 0;

		size = *in_size;
		total_size = size * size;
	}

	// This tells the software whether the search space has been completed or not
	// And therefore whether to start the IP core again once the puzzle solution has been retrieved
	uint1 cont = 0;

	// This while loop allows search throughout all of the defined search space from start_idx to end_idx
	main_loop:while(stack[0].idx < end_idx)
	{
		// If the abort pin is set high then we want to break out of the loop which stops execution of the ip core
		if (*abort == 1)
			break;


		// This finds and inserts a valid tile into the working solution if there is one
		// If there isn't then we back track
		uint1 succ = get_tile();
		if (!succ)
		{
			backtrack();
		}
		// If it was successful and we've filled the solution grid then write the solution to memory and exit
		// And tell the software that there is still search space left to search
		else if (succ && current_idx == total_size)
		{
			memcpy(ram, &current_grid, MAX_TILES * sizeof(uint32));
			cont = 1;
			break;
		}
	}

	return cont;
}

uint1 get_tile()
{
	// When search for a tile we search from the last position where a tile was successfully found in the current working solution
	// before a back track until the end of the tiles array
	tile_check_loop:for (uint8 idx = stack[current_idx].idx; idx < total_size; idx++)
	{
		// If the tile is not used then we can check to see if the tile is valid
		if (!used[idx])
		{
			// Perform some check to see if this tile in any rotation fits
			// If that check passes we want to check the rotation count and the index used and mark the tile used
			if (check_tile(idx, tiles[idx], 0))
				return 1;
		}
	}

	return 0;
}

void backtrack()
{
	stack[current_idx].idx = 0;
	stack[current_idx].rot = 0;
	dec_current();
	uint8 idx = stack[current_idx].idx;
	used[idx] = 0;
	uint3 init_rot = stack[current_idx].rot;
	uint32 tile = clockwise_rotate(tiles[idx]);

	// We want to check the last successful tile in all other possible rotations to see if it still fits in that position
	// If it does not then increment the index stored in the stack so we don't check that tile again since we know it is
	// not valid
	// We then exit back to the main loop so it can try to get another tile to fit
	if (!check_tile(idx, tile, init_rot + 1))
	{
		current_grid[current_idx] = 0;
		stack[current_idx].idx++;
		stack[current_idx].rot = 0;
	}
}

uint1 check_tile(uint8 idx, uint32 tile, uint3 init_rot)
{
	// We want to check the tile in all possible rotations
	rotation_check:for (uint3 rot = init_rot; rot < 4; rot++)
	{
		uint1 valid_top, valid_left;

		// If it's not at the top then check the colour match
		if (current_y != 0)
		{
			valid_top = BOTTOM(current_grid + current_idx - size) == TOP(&tile);
		}
		else
			valid_top = 1;

		// If it's not at the very left then check the colour match
		if (current_x != 0)
		{
			valid_left= RIGHT(current_grid + current_idx - 1) == LEFT(&tile);
		}
		else
			valid_left = 1;

		// If the tile is valid then we add the tile to the current grid
		// Set the last tried position on the stack along with the rotation to enable back tracking
		// Otherwise we try the next rotation
		if (valid_left && valid_top)
		{
			stack[current_idx].idx = idx;
			stack[current_idx].rot = rot;
			tiles[idx] = tile;
			current_grid[current_idx] = tile;
			used[idx] = 1;
			inc_current();
			return 1;
		}
		else
			tile = clockwise_rotate(tile);
	}
	return 0;
}

uint32 clockwise_rotate(uint32 tile)
{
	uint5 tmp = TOP(&tile);
	TOP(&tile) = LEFT(&tile);
	LEFT(&tile) = BOTTOM(&tile);
	BOTTOM(&tile) = RIGHT(&tile);
	RIGHT(&tile) = tmp;
	return tile;
}

void inc_current()
{
	current_idx++;
	if (current_x == size - 1)
	{
		current_x = 0;
		current_y++;
	}
	else
	{
		current_x++;
	}
}

void dec_current()
{
	current_idx--;
	if (current_x == 0)
	{
		current_x = size -1;
		current_y--;
	}
	else
	{
		current_x--;
	}
}
