#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <sys/ttycom.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#include <Windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <sys/ioctl.h>
#include <unistd.h>
#endif // Windows/Linux

int WIDTH = 80;
int HEIGHT = 30;

typedef struct {
    float x, y, z;
} Vertex;

typedef struct {
    int v1, v2, v3;
} Face;

Vertex *vertices = NULL;
Face *faces = NULL;

int num_vertices = 8;
int num_faces = 12;

typedef struct {
    Vertex min;
    Vertex max;
    Vertex center;
    float size;
} BoundingBox;

BoundingBox calculate_bounding_box(Vertex *vertices, int num_vertices) {
    BoundingBox bbox;
    bbox.min = (Vertex){FLT_MAX, FLT_MAX, FLT_MAX};
    bbox.max = (Vertex){-FLT_MAX, -FLT_MAX, -FLT_MAX};

    for (int i = 0; i < num_vertices; i++) {
        if (vertices[i].x < bbox.min.x) bbox.min.x = vertices[i].x;
        if (vertices[i].y < bbox.min.y) bbox.min.y = vertices[i].y;
        if (vertices[i].z < bbox.min.z) bbox.min.z = vertices[i].z;
        if (vertices[i].x > bbox.max.x) bbox.max.x = vertices[i].x;
        if (vertices[i].y > bbox.max.y) bbox.max.y = vertices[i].y;
        if (vertices[i].z > bbox.max.z) bbox.max.z = vertices[i].z;
    }

    bbox.center.x = (bbox.min.x + bbox.max.x) / 2;
    bbox.center.y = (bbox.min.y + bbox.max.y) / 2;
    bbox.center.z = (bbox.min.z + bbox.max.z) / 2;

    float size_x = bbox.max.x - bbox.min.x;
    float size_y = bbox.max.y - bbox.min.y;
    float size_z = bbox.max.z - bbox.min.z;

    bbox.size = fmaxf(size_x, fmaxf(size_y, size_z));

    return bbox;
}


void multiply_matrix_vector(Vertex *v, float mat[4][4]) {
    float x = v->x, y = v->y, z = v->z;
    v->x = x * mat[0][0] + y * mat[1][0] + z * mat[2][0] + mat[3][0];
    v->y = x * mat[0][1] + y * mat[1][1] + z * mat[2][1] + mat[3][1];
    v->z = x * mat[0][2] + y * mat[1][2] + z * mat[2][2] + mat[3][2];
}

void rotation_matrix(float A, float B, float mat[4][4]) {
    float cosA = cos(A), sinA = sin(A);
    float cosB = cos(B), sinB = sin(B);

    mat[0][0] = cosB;
    mat[0][1] = 0;
    mat[0][2] = -sinB;
    mat[0][3] = 0;

    mat[1][0] = sinA * sinB;
    mat[1][1] = cosA;
    mat[1][2] = sinA * cosB;
    mat[1][3] = 0;

    mat[2][0] = cosA * sinB;
    mat[2][1] = -sinA;
    mat[2][2] = cosA * cosB;
    mat[2][3] = 0;

    mat[3][0] = 0;
    mat[3][1] = 0;
    mat[3][2] = 0;
    mat[3][3] = 1;
}

Vertex cross_product(Vertex a, Vertex b) {
    Vertex result;
    result.x = a.y * b.z - a.z * b.y;
    result.y = a.z * b.x - a.x * b.z;
    result.z = a.x * b.y - a.y * b.x;
    return result;
}

Vertex translate(Vertex v, Vertex T)
{
    v.x += T.x;
    v.y += T.y;
    v.z += T.z;
    return v;
}

Vertex normalize(Vertex v) {
    float length = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    v.x /= length;
    v.y /= length;
    v.z /= length;
    return v;
}

float dot_product(Vertex a, Vertex b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

void draw_triangle(Vertex v0, Vertex v1, Vertex v2, float zbuf[], char buf[], char shade) {
    int min_x = fmax(0, fmin(v0.x, fmin(v1.x, v2.x)));
    int max_x = fmin(WIDTH - 1, fmax(v0.x, fmax(v1.x, v2.x)));
    int min_y = fmax(0, fmin(v0.y, fmin(v1.y, v2.y)));
    int max_y = fmin(HEIGHT - 1, fmax(v0.y, fmax(v1.y, v2.y)));

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            float denominator = (v1.y - v2.y) * (v0.x - v2.x) + (v2.x - v1.x) * (v0.y - v2.y);
            float a = ((v1.y - v2.y) * (x - v2.x) + (v2.x - v1.x) * (y - v2.y)) / denominator;
            float b = ((v2.y - v0.y) * (x - v2.x) + (v0.x - v2.x) * (y - v2.y)) / denominator;
            float c = 1.0f - a - b;

            if (a >= 0 && b >= 0 && c >= 0) {
                float z = a * v0.z + b * v1.z + c * v2.z;
                int index = y * WIDTH + x;
                if (z > zbuf[index]) {
                    zbuf[index] = z;
                    buf[index] = shade;
                }
            }
        }
    }
}

void read_obj_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Unable to open file");
        exit(EXIT_FAILURE);
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "v ", 2) == 0) {
            num_vertices++;
        } else if (strncmp(line, "f ", 2) == 0) {
            num_faces++;
        }
    }

    vertices = (Vertex *)malloc(num_vertices * sizeof(Vertex));
    faces = (Face *)malloc(num_faces * sizeof(Face));

    fseek(file, 0, SEEK_SET);
    int v_idx = 0, f_idx = 0;
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "v ", 2) == 0) {
            sscanf(line, "v %f %f %f", &vertices[v_idx].x, &vertices[v_idx].y, &vertices[v_idx].z);
            v_idx++;
        } else if (strncmp(line, "f ", 2) == 0) {
            int v1, v2, v3;
            if (sscanf(line, "f %d%*[^ ] %d%*[^ ] %d", &v1, &v2, &v3) == 3 ||
                sscanf(line, "f %d %d %d", &v1, &v2, &v3) == 3) {
                faces[f_idx].v1 = v1 - 1; // Convert to 0-based index
                faces[f_idx].v2 = v2 - 1; // Convert to 0-based index
                faces[f_idx].v3 = v3 - 1; // Convert to 0-based index
                f_idx++;
            } else {
                fprintf(stderr, "Unsupported face format: %s", line);
            }
        }
    }
    fclose(file);
}

void export_obj_file(const char *filename) {
    FILE *file = fopen(filename, "w");
    if (!file) {
        perror("Unable to open file for writing");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < num_vertices; i++) {
        fprintf(file, "v %f %f %f\n", vertices[i].x, vertices[i].y, vertices[i].z);
    }

    for (int i = 0; i < num_faces; i++) {
        faces[i].v1++;
        faces[i].v2++;
        faces[i].v3++;
        fprintf(file, "f %d %d %d\n", faces[i].v1, faces[i].v2, faces[i].v3);
    }

    fclose(file);
}

int main() {
    #if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    #define WIDTH (int)(csbi.srWindow.Right-csbi.srWindow.Left+1)
    #define HEIGHT (int)(csbi.srWindow.Bottom-csbi.srWindow.Top+1)
    #elif defined(__linux__) || defined(__APPLE__) 
    struct winsize w;
    ioctl(0, TIOCGWINSZ, &w);
    WIDTH = (int)(w.ws_col);
    HEIGHT = (int)(w.ws_row);
    #endif // Windows/Linux


    read_obj_file("Torus.obj");

    BoundingBox bbox = calculate_bounding_box(vertices, num_vertices);

    // Adjust camera position and projection
    float camera_distance = bbox.size; // Adjust this factor to ensure the object fits well
    float A = 0, B = 0;

    float z[WIDTH * HEIGHT];//z-buffer
    char b[WIDTH * HEIGHT];//ascii encoded z-buffer

    Vertex light_dir = {0, 0.5, 0.5};  // Simple light direction
    light_dir = normalize(light_dir);

    printf("\x1b[2J");
    for (;;) {
        memset(b, 32, WIDTH * HEIGHT);
        memset(z, 0, WIDTH * HEIGHT * sizeof(float));

        float rotation[4][4];
        rotation_matrix(A, B, rotation);

        for (int f = 0; f < num_faces; f++) {
            Face face = faces[f];

            Vertex v1 = vertices[face.v1];
            Vertex v2 = vertices[face.v2];
            Vertex v3 = vertices[face.v3];

            Vertex negBboxCenter = (Vertex){-bbox.center.x, -bbox.center.y, -bbox.center.z};
            v1 = translate(v1, negBboxCenter);
            v2 = translate(v2, negBboxCenter);
            v2 = translate(v3, negBboxCenter);

            multiply_matrix_vector(&v1, rotation);
            multiply_matrix_vector(&v2, rotation);
            multiply_matrix_vector(&v3, rotation);

            v1 = translate(v1, bbox.center);
            v2 = translate(v2, bbox.center);
            v3 = translate(v3, bbox.center);

            v1.z += camera_distance;
            v2.z += camera_distance;
            v3.z += camera_distance;

            v1.x = WIDTH / 2 + (WIDTH / 2) * v1.x / v1.z;
            v1.y = HEIGHT / 2 - (HEIGHT / 2) * v1.y / v1.z;
            v2.x = WIDTH / 2 + (WIDTH / 2) * v2.x / v2.z;
            v2.y = HEIGHT / 2 - (HEIGHT / 2) * v2.y / v2.z;
            v3.x = WIDTH / 2 + (WIDTH / 2) * v3.x / v3.z;
            v3.y = HEIGHT / 2 - (HEIGHT / 2) * v3.y / v3.z;

            Vertex normal = cross_product(
                (Vertex){v2.x - v1.x, v2.y - v1.y, v2.z - v1.z},
                (Vertex){v3.x - v1.x, v3.y - v1.y, v3.z - v1.z}
            );
            normal = normalize(normal);

            float intensity = dot_product(normal, light_dir);
            int N = (int)(12 * intensity);

            char shade = ".,-~:;=!*#$@"[N > 0 ? N : 0];

            draw_triangle(v1, v2, v3, z, b, shade);
        }

        printf("\x1b[d");
        for (int k = 0; k < WIDTH * HEIGHT; k++) {
            putchar(k % WIDTH ? b[k] : 10);
        }

        //A += 0.002;
        B += 0.001;
    }

    free(vertices);
    free(faces);

    return 0;
}

