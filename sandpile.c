#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <immintrin.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

static void die(const char *s) {
  fprintf(stderr, "%s\n", s);
  exit(1);
}

int N = 801;
int8_t **grid;
bool **gridcond;
int *gridbounds;

// you can also use this function to debug print your sandpile
static void render(char *filename) {
  int chan = 3;
  unsigned char *pixels = (unsigned char *)malloc(N * N * chan * sizeof(char));
  int stride = N * chan;
  if (!pixels)
    die("malloc failed for PNG pixel buffer");
  for (int i = 0; i < N; ++i) {
    for (int j = 0; j < N; ++j) {
      unsigned char r = 0, g = 0, b = 0;
      switch (grid[i][j]) {
      case 0: // obsidian
        r = 15;
        g = 15;
        b = 25;
        break;
      case 1: // electric cyan
        r = 0;
        g = 210;
        b = 255;
        break;
      case 2: // vibrant magenta
        r = 230;
        g = 30;
        b = 130;
        break;
      case 3: // bright gold
        r = 255;
        g = 200;
        b = 0;
        break;
      default: // white
        r = 0;
        g = 0;
        b = 0;
      }

      int pid = (i * N + j) * chan;
      pixels[pid] = r;
      pixels[pid + 1] = g;
      pixels[pid + 2] = b;
    }
  }
  if (stbi_write_png(filename, N, N, chan, pixels, stride)) {
    printf("Successfully saved sandpile identity to: %s\n", filename);
  } else {
    die("Error: stb_image_write failed to save png file\n");
  }
  free(pixels);
}

static void stabilize(int8_t** __restrict__ grid,
                      bool** __restrict__ gridcond,
                      int* __restrict__ gridbounds) {
  bool changed;
  do {
    changed = false;

    // use a two-phase algo
    // phase 1: topple-check, local sub
    // phase 2: cross-pile gather

    for (int i = 0; i < N; ++i) {
      int j;
      /* __m512i four = _mm512_set1_epi8(4); */
      /* for (j = gridbounds[2*i]; gridbounds[2*i + 1] - j >= 64; j+=64) { */
      /*   __m512i vec = _mm512_loadu_si512(&(grid[i][j])); */
      /*   __mmask64 condvec = _mm512_test_epi8_mask(vec, four); */
      /*   changed |= (condvec != 0); */

      /*   vec = _mm512_mask_sub_epi8(vec, condvec, vec, four); */
      /*   grid[i][j] -= ((int8_t)gridcond[i][j+1]) << 2; */
      /*   _mm512_storeu_si512(&(grid[i][j]), vec); */

      /*   // store the condvec */
      /*   __m512i condvecex = _mm512_maskz_set1_epi8(condvec, 1); */
      /*   _mm512_storeu_si512(&(gridcond[i][j+1]), condvecex); */
      /* } */

      for (j = gridbounds[2*i]; j <= gridbounds[2*i + 1]; ++j) {
        gridcond[i][j+1] = grid[i][j] >= 0x4;
        changed |= gridcond[i][j+1]; // topple it
        grid[i][j] -= ((int8_t)gridcond[i][j+1]) * 4;
      }

      for (int j = gridbounds[2*i]; j <= gridbounds[2*i + 1]; ++j) {
        grid[i][j] += (int8_t)gridcond[i][j]; // left
        grid[i][j] += (int8_t)gridcond[i][j+2]; //right
      }
    }

    for (int i=0; i < N; i++) {
      for (int j = gridbounds[2*i]; j <= gridbounds[2*i + 1]; ++j) {
        if (i > 0) // up
          grid[i][j] += (int8_t)gridcond[i-1][j+1];
        if (i < N - 1) // down
          grid[i][j] += (int8_t) gridcond[i+1][j+1];
      }
    }
  } while (changed);
}

void find_identity() {
  // we can precompute a "change" mask for this.
  for (int i = 0; i < N; ++i) {
    for (int j = 0; j < N; ++j) {
      grid[i][j] = 6;
    }
  }
  stabilize(grid, gridcond, gridbounds);
  for (int i = 0; i < N; ++i) {
    for (int j = 0; j < N; ++j) {
      grid[i][j] = 6 - grid[i][j];
    }
  }
  stabilize(grid, gridcond, gridbounds);
}

int main(int argc, char **argv) {
  if (argc < 2 || argc > 2) {
    fprintf(stderr, "Usage: %s N\n", argv[0]);
    return EXIT_FAILURE;
  }
  char *end;
  uintmax_t parsed = strtoumax(argv[1], &end, 10);
  if (errno || argv[1][0] < '0' || argv[1][0] > '9' || *end || !parsed ||
      parsed > INT32_MAX)
    die("grid size must be a positive int");
  N = (int)parsed;

  grid = (int8_t **)malloc(N * sizeof(int8_t *));
  for (int i = 0; i < N; ++i) {
    grid[i] = (int8_t *)malloc(N * sizeof(int8_t));
  }

  gridbounds = (int*)malloc(2 * N * sizeof(int));
  for (int i=0; i<N; i++) {
    // lower bound is always based on circle
    int jcirc_base = (int)sqrt((N/2)*(N/2) - (i - N/2)*(i - N/2));
    gridbounds[2*i] = MAX(0, N/2 - jcirc_base);

    // two possible upper bounds -- circle, and *pacman*
    int jpac = MAX(i, N - i - 1);
    gridbounds[2*i+1] = MIN(jpac, N/2 + jcirc_base);
  }

  gridcond = (bool**)malloc(N * sizeof(bool*));
  for (int i = 0; i < N; ++i) {
    // add "halo" cells for this array
    gridcond[i] = (bool*)calloc(N+2, sizeof(bool));
  }

  find_identity();

  render("identity.png");
  for (int i = 0; i < N; ++i) {
    free(grid[i]);
    free(gridcond[i]);
  }
  free(grid);
  free(gridcond);
  free(gridbounds);
  return EXIT_SUCCESS;
}
