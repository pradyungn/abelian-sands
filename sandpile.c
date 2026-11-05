#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <immintrin.h>
#include <omp.h>
#include <time.h>

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

inline bool sideways_pass (int8_t** __restrict__ grid,
                           bool** __restrict__ gridcond,
                           int* __restrict__ gridbounds,
                           int Ns, int Ne) {
  bool changed = false;
  for (int i = Ns; i < Ne; ++i) {
    int j = gridbounds[2*i];
    int jmax = gridbounds[2*i + 1];
    const __m512i four = _mm512_set1_epi8(4);
    const __m512i one = _mm512_set1_epi8(1);

    bool fflag = false;

    for (; j <= jmax; j+=64) {
      int count = MIN(64, jmax - j + 1);
      __mmask64 active_vec = ((__mmask64)~(0ULL)) >> (64 - count); // lot more dynamic instruction atm

      __m512i vec = _mm512_maskz_loadu_epi8(active_vec, &(grid[i][j]));
      __mmask64 condvec = _mm512_test_epi8_mask(vec, four);
      changed |= (condvec != 0);
      vec = _mm512_mask_sub_epi8(vec, condvec, vec, four);

      // in parallel, generate the "update" vec
      __mmask64 l_condvec = condvec << 1;
      __mmask64 r_condvec = condvec >> 1;
      __m512i l_vec = _mm512_maskz_set1_epi8(l_condvec, 1);
      __m512i uvec = _mm512_mask_add_epi8(l_vec, r_condvec, l_vec, one);

      vec = _mm512_add_epi8(vec, uvec);

      if (i > Ns) {
        __m512i up_condvec = _mm512_maskz_loadu_epi8(active_vec, &(gridcond[i-1][j+1]));
        vec = _mm512_add_epi8(vec, up_condvec);
      }

      _mm512_mask_storeu_epi8(&(grid[i][j]), active_vec, vec);

      // store the condvec
      __m512i condvecex = _mm512_maskz_set1_epi8(condvec, 1);
      _mm512_mask_storeu_epi8(&(gridcond[i][j+1]), active_vec, condvecex);


      // stitch halo elements per block
      grid[i][j] += gridcond[i][j];
      if (fflag) {
        grid[i][j-1] += gridcond[i][j+1];
      } else {
        fflag = true;
      }
    }
  }

  return changed;
}

inline void updown_pass(int8_t** __restrict__ grid,
                        bool** __restrict__ gridcond,
                        int* __restrict__ gridbounds,
                        int Ns, int Ne) {
  for (int i=Ns; i < Ne-1; i++) {
    int j = gridbounds[2*i];
    int jmax = gridbounds[2*i + 1];
    for (; j <= jmax; j+=64) {
      int count = MIN(64, jmax - j + 1);
      __mmask64 active_vec = ((__mmask64)~(0ULL)) >> (64 - count);
      __m512i vec = _mm512_maskz_loadu_epi8(active_vec, &(grid[i][j]));

      // accumulate from below
      __m512i condvec = _mm512_maskz_loadu_epi8(active_vec, &(gridcond[i+1][j+1]));
      vec = _mm512_add_epi8(vec, condvec);
      _mm512_mask_storeu_epi8(&(grid[i][j]), active_vec, vec);
    }
  }
}

inline void stabilize_kernel(int8_t** __restrict__ grid,
                             bool** __restrict__ gridcond,
                             int* __restrict__ gridbounds,
                             int* epoch_flag) {
  for (int epoch=0; ; epoch++) {
    // use a two-phase algo
    // phase 1: topple-check, local sub, sideways gather
    // phase 2: cross-pile gather from adjacent rows

    bool lc_changed = false;
    const int tid = omp_get_thread_num();
    const int tct = omp_get_num_threads();

    int BLOCKS = tct; // hardcoded to core count, for now
    int BLOCKSIZE = (N+BLOCKS-1)/BLOCKS;

    int Ns = BLOCKSIZE*tid;
    int Ne = MIN(N, Ns+BLOCKSIZE);
    lc_changed |= sideways_pass(grid, gridcond, gridbounds, Ns, Ne);
    updown_pass(grid, gridcond, gridbounds, Ns, Ne);

    if (lc_changed) {
      #pragma omp atomic write
      *epoch_flag = epoch;
    }
    #pragma omp barrier

    if (tid > 0) {
      int i = tid*BLOCKSIZE;
      int j = MAX(gridbounds[2*i], gridbounds[2*(i-1)]);
      int jmax = MIN(gridbounds[2*i + 1], gridbounds[2*(i-1) + 1]);

      for (; j <= jmax; j+=64) {
        int count = MIN(64, jmax - j + 1);
        __mmask64 active_vec = ((__mmask64)~(0ULL)) >> (64 - count);

        __m512i vec = _mm512_maskz_loadu_epi8(active_vec, &(grid[i][j]));
        __m512i condvec = _mm512_maskz_loadu_epi8(active_vec, &(gridcond[i-1][j+1]));
        vec = _mm512_add_epi8(vec, condvec);
        _mm512_mask_storeu_epi8(&(grid[i][j]), active_vec, vec);

        vec = _mm512_maskz_loadu_epi8(active_vec, &(grid[i-1][j]));
        condvec = _mm512_maskz_loadu_epi8(active_vec, &(gridcond[i][j+1]));
        vec = _mm512_add_epi8(vec, condvec);
        _mm512_mask_storeu_epi8(&(grid[i-1][j]), active_vec, vec);
      }
    }

    if (*epoch_flag < epoch) break;
    #pragma omp barrier
  }
}

static void stabilize(int8_t** __restrict__ grid,
                      bool** __restrict__ gridcond,
                      int* __restrict__ gridbounds) {
  int changed_epoch = -1;
  int second_epoch = -1;

  #pragma omp parallel
  {
    // initial stabilization
    stabilize_kernel(grid, gridcond, gridbounds, &changed_epoch);

    // invert owned region -- block params matched to stabkern
    int BLOCKS = omp_get_num_threads();
    int BLOCKSIZE = (N+BLOCKS-1)/BLOCKS;

    int Ns = BLOCKSIZE*omp_get_thread_num();
    int Ne = MIN(Ns + BLOCKSIZE, N);

    for (int i = Ns; i < Ne; ++i) {
      for (int j = 0; j < N; ++j) {
        grid[i][j] = 6 - grid[i][j];
      }
    }

    // run stabilization again
    stabilize_kernel(grid, gridcond, gridbounds, &second_epoch);
  }
}

void find_identity() {
  // we can precompute a "change" mask for this.
  for (int i = 0; i < N; ++i) {
    for (int j = 0; j < N; ++j) {
      grid[i][j] = 6;
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
