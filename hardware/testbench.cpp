//top: 6, bottom: 5, left: 1, right: 6
//top: 4, bottom: 6, left: 6, right: 7
//top: 6, bottom: 2, left: 7, right: 3
//top: 0, bottom: 7, left: 6, right: 4

#include "toplevel.h"
#include <stdio.h>
#define MAKE_TILE(c1, c2, c3, c4) ((c1 << 24) | (c2 <<16) | (c3 << 8) | (c4))
#define TOP(x) 		((uint8_t *)(x))[0]
#define BOTTOM(x) 	((uint8_t *)(x))[1]
#define LEFT(x) 	((uint8_t *)(x))[2]
#define RIGHT(x) 	((uint8_t *)(x))[3]


int main()
{
	printf("making tiles\n");
	uint32_t tiles[100] = {0};
	printf("making tiles\n");
	tiles[0] = MAKE_TILE(6, 5, 1, 6);
	tiles[1] = MAKE_TILE(4, 6, 6, 7);
	tiles[2] = MAKE_TILE(6, 2, 7, 3);
	tiles[3] = MAKE_TILE(0, 7, 6, 4);

	uint32_t sol;

	toplevel(tiles, 2, 0, 2 * 2, &sol);



	for (int i = 0; i < 4; i++)
	{
		uint32_t tile = tiles[i];
		uint8_t top, bottom, left, right;
		top = TOP(&tile);
		bottom = BOTTOM(&tile);
		left = LEFT(&tile);
		right = RIGHT(&tile);
		printf("top: %d, bottom: %d, left: %d, right: %d\n", top, bottom, left, right);
	}
}
