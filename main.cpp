#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <cstdlib>
#include <string>
#include <Windows.h>
#include <vector>
#include <ctime>
#include <math.h>
#include <numeric>
#include <thread>
#include <mutex>

#define map_size 1024

#include <internal/shader_loader.h>
#include <internal/terrain_generation.h>
#include <internal/loading_screen.h>

#define WINDOW_WIDTH 2048
#define WINDOW_HEIGHT 1024

//#define max(a, b) a > b ? a : b
//#define min(a, b) a < b ? a : b

GLFWwindow* window;

bool init();
void quit();

bool load_model(const char * obj_path, const char * mtl_path, std::vector<glm::vec3>& out_vertices, std::vector<glm::vec3>& out_colors, std::vector<glm::vec3>& out_normals);

int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) // this needs to be replaced with something cross-platform, but Visual Studio won't complile anything without this
{
	#ifdef _DEBUG
		AllocConsole();
		FILE *dummy;
		freopen_s(&dummy, "CONOUT$", "w", stdout);
		freopen_s(&dummy, "CONOUT$", "w", stderr);
	#endif

	if (!init())
	{
		return 1;
	}

	GLuint program_id = load_shaders("vertex_shader.glsl", "fragment_shader.glsl");

	GLuint vertex_array_id;
	glGenVertexArrays(1, &vertex_array_id);
	glBindVertexArray(vertex_array_id);
	
	std::vector<glm::vec3> vertices;
	std::vector<std::vector<glm::vec3>> map;
	std::vector<glm::vec3> colors;
	std::vector<glm::vec3> normals;

	int terrain_completion = 0;
	std::thread terrain_thread(generate_terrain, map_size, 0, 0.25, std::ref(map), std::ref(colors), std::ref(normals), std::ref(terrain_completion));
	loading_screen(window, &terrain_completion, program_id, glm::vec3(1, 1, 1), glm::vec3(0, 1, 0), glm::vec3(0, 0, 0));
	terrain_thread.join();

	for (int x = 0; x < map_size-1; x++) 
	{
		for (int z = 0; z < map_size-1; z++)
		{
			vertices.push_back(map[x + 1][z]);
			vertices.push_back(map[x][z]);
			vertices.push_back(map[x + 1][z + 1]);

			vertices.push_back(map[x][z]);
			vertices.push_back(map[x][z + 1]);
			vertices.push_back(map[x + 1][z + 1]);
		}
	}

	for (int i = 0; i < 6; i++)
		vertices.push_back(map[map_size][i]);

	int vertex_count = vertices.size();

	// buffers for position and color
	GLuint vertex_buffer;
	GLuint color_buffer;
	GLuint normal_buffer;

	glGenBuffers(1, &vertex_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec3), &vertices[0], GL_STATIC_DRAW);
	
	glGenBuffers(1, &color_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, color_buffer);
	glBufferData(GL_ARRAY_BUFFER, colors.size() * sizeof(glm::vec3), &colors[0], GL_STATIC_DRAW);

	glGenBuffers(1, &normal_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, normal_buffer);
	glBufferData(GL_ARRAY_BUFFER, normals.size() * sizeof(glm::vec3), &normals[0], GL_STATIC_DRAW);

	// movement variables
	glm::vec3 position = glm::vec3(0, map_size, 0);
	float h_angle = 3.14f; // radians
	float v_angle = 0.0f;
	const float initial_fov = 90.0f;

	const float speed = 100.0f;
	const float mouse_speed = 0.5f;

	// matricies
	glm::mat4 projection;
	glm::mat4 view;
	glm::mat4 model;
	glm::mat4 final_matrix;

	// handle for the matrices in the shaders
	GLuint matrix_id = glGetUniformLocation(program_id, "matrix");
	GLuint view_id = glGetUniformLocation(program_id, "view");
	GLuint model_id = glGetUniformLocation(program_id, "model");

	// ambient lighting
	glm::vec3 ambient_light_color = glm::vec3(0.2f, 0.2f, 0.2f);
	GLuint ambient_light_id = glGetUniformLocation(program_id, "ambient_light_color");

	// sun variables
	float sun_angle = 45;
	const float sun_speed = 0.1;
	float sun_brightness = sin(glm::radians(sun_angle));
	glm::vec3 directional_light_color = glm::vec3(0.9f * sun_brightness/2.5, 0.9f * sun_brightness, 0.9f * sun_brightness);
	glm::vec3 directional_light_direction = glm::vec3(0.0f, 0.0f, 0.0f);
	GLuint directional_light_color_id = glGetUniformLocation(program_id, "directional_light_color");
	GLuint directional_light_direction_id = glGetUniformLocation(program_id, "directional_light_direction");
	glClearColor(sun_brightness * 0.529, sun_brightness * 0.808, sun_brightness * 0.922, 1);

	// fog variables
	float fog_start = 256;
	float fog_end = 512;
	glm::vec3 fog_color(0.529, 0.808, 0.922);

	// uniform variables to pass to shaders
	GLuint camera_pos_id = glGetUniformLocation(program_id, "camera_pos");
	GLuint fog_start_id = glGetUniformLocation(program_id, "fog_start");
	GLuint fog_end_id = glGetUniformLocation(program_id, "fog_end");
	GLuint fog_color_id = glGetUniformLocation(program_id, "fog_color");

	// delta_time calculation variables
	double current_time = glfwGetTime();
	double last_time = current_time;

	// physics variables
	const float gravity = 0.1f;
	const float max_fall_speed = 5.0f;
	float y_speed = 0;

	// main loop 
	while (!glfwWindowShouldClose(window))
	{	
		// calculate deltatime
		current_time = glfwGetTime();
		float delta_time = float(current_time - last_time);
		last_time = current_time;

		// handle mouse movement
		double xpos, ypos;
		glfwGetCursorPos(window, &xpos, &ypos);
		glfwSetCursorPos(window, WINDOW_WIDTH / 2, WINDOW_HEIGHT / 2);

		h_angle += mouse_speed * delta_time * float(WINDOW_WIDTH / 2 - xpos);
		v_angle += mouse_speed * delta_time * float(WINDOW_HEIGHT / 2 - ypos);
		
		if (glm::degrees(v_angle) < -90) v_angle = glm::radians(-90.0f);
		if (glm::degrees(v_angle) > 90) v_angle = glm::radians(90.0f);

		// direction player is pointing in
		glm::vec3 direction(cos(v_angle) * sin(h_angle), sin(v_angle), cos(v_angle) * cos(h_angle));

		// forward movement 
		glm::vec3 forward(sin(h_angle), 0, cos(h_angle));

		// to the right of the player
		glm::vec3 right = glm::vec3(sin(h_angle - 3.14f / 2.0f), 0, cos(h_angle - 3.14f / 2.0f));

		glm::vec3 up = glm::cross(right, direction);

		sun_angle += delta_time * sun_speed;
		sun_brightness = sin(glm::radians(sun_angle));
		directional_light_direction = glm::vec3(cos(glm::radians(sun_angle)), sin(glm::radians(sun_angle)), 0);
		directional_light_color = glm::vec3(0.9f * sun_brightness, 0.9f * sun_brightness, 0.9f * sun_brightness);

		// handle keyboard input
		glfwPollEvents();
		if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) position += forward * delta_time * speed; // move forward
		if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) position -= forward * delta_time * speed; // move backward
		if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) position -= right * delta_time * speed;   // move left
		if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) position += right * delta_time * speed;   // move right

		// gravity and physics
		float ground_pos;
		if (position.x > -map_size / 2 && position.x < map_size / 2 && position.z > -map_size / 2 && position.z < map_size / 2)
			ground_pos = (float)bilinear_interpolation(
				map[(int)position.x + map_size / 2][(int)position.z + map_size / 2].y,
				map[(int)position.x + map_size / 2][(int)position.z + 1 + map_size / 2].y,
				map[(int)position.x + 1 + map_size / 2][(int)position.z + map_size / 2].y,
				map[(int)position.x + 1 + map_size / 2][(int)position.z + 1 + map_size / 2].y,
				(int)position.x + map_size / 2,
				(int)position.x + 1 + map_size / 2,
				(int)position.z + map_size / 2,
				(int)position.z + 1 + map_size / 2,
				position.x + map_size / 2,
				position.z + map_size / 2
			);
		else
			ground_pos = 0;

		position.y += y_speed; 
		if (abs(position.y - ground_pos - 5) < gravity)
			if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
				y_speed = 2;
			else
				y_speed = 0;
		else if (position.y < ground_pos + 5)
		{
			position.y = ground_pos + 5;
			y_speed = 0;
		}
		else 
			y_speed -= gravity;

		projection = glm::perspective(glm::radians(initial_fov), 4.0f / 3.0f, 0.1f, 1024.0f);
		view = glm::lookAt(position, position + direction, up);
		model = glm::mat4(1.0f);
		final_matrix = projection * view * model;

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		
		glUseProgram(program_id);

		// send stuff to shaders
		glUniformMatrix4fv(matrix_id, 1, GL_FALSE, &final_matrix[0][0]);
		glUniformMatrix4fv(view_id, 1, GL_FALSE, &view[0][0]);
		glUniformMatrix4fv(model_id, 1, GL_FALSE, &model[0][0]);

		glUniform3fv(ambient_light_id, 1, &ambient_light_color[0]);
		glUniform3fv(directional_light_color_id, 1, &directional_light_color[0]);
		glUniform3fv(directional_light_direction_id, 1, &directional_light_direction[0]);

		glUniform3fv(camera_pos_id, 1, &position[0]);
		glUniform3fv(fog_color_id, 1, &fog_color[0]);
		glUniform1f(fog_start_id, fog_start);
		glUniform1f(fog_end_id, fog_end);

		glEnableVertexAttribArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
		glVertexAttribPointer(
			0, // attribute No 0
			3, // size
			GL_FLOAT, // type
			GL_FALSE, // is it normalized?
			0, // gap between groups of data
			(void*)0 // offset from start
		);

		glEnableVertexAttribArray(1);
		glBindBuffer(GL_ARRAY_BUFFER, color_buffer);
		glVertexAttribPointer(
			1, // attribute No 0
			3, // size
			GL_FLOAT, // type
			GL_FALSE, // is it normalized?
			0, // gap between groups of data
			(void*)0 // offset from start
		);

		glEnableVertexAttribArray(2);
		glBindBuffer(GL_ARRAY_BUFFER, normal_buffer);
		glVertexAttribPointer(
			2, // attribute No 0
			3, // size
			GL_FLOAT, // type
			GL_FALSE, // is it normalized?
			0, // gap between groups of data
			(void*)0 // offset from start
		);

		glDrawArrays(GL_TRIANGLES, 0, vertex_count);
		glDisableVertexAttribArray(0);
		glDisableVertexAttribArray(1);
		glDisableVertexAttribArray(2);

		glfwSwapBuffers(window);
	}

	quit();
	return 0;
}	

bool load_model(const char * obj_path, const char * mtl_path, 
	std::vector<glm::vec3>& out_vertices,
	std::vector<glm::vec3>& out_colors,
	std::vector<glm::vec3>& out_normals)
{
	std::vector<unsigned int> vertex_indices, color_indices, normal_indices;
	std::vector<glm::vec3> temp_vertices, temp_colors, temp_normals;
	int color_index = -1;

	FILE * file;

	printf("loading object file %s ...\n", obj_path);
	fopen_s(&file, obj_path, "r");
	if (file == NULL)
	{
		printf("could not open object file!\n");
		return false;
	}
	while (true)
	{
		char line_header[128];

		// read first word of line
		int res = fscanf_s(file, "%s", line_header, 128);
		if (res == EOF)
		{
			printf("EOF found\n");
			break;
		}
		if (strcmp(line_header, "v") == 0)
		{
			// vertex position
			glm::vec3 vertex;
			fscanf_s(file, "%f %f %f", &vertex.x, &vertex.y, &vertex.z);
			temp_vertices.push_back(vertex);
		}
		else if (strcmp(line_header, "vn") == 0)
		{
			// vertex normal
			glm::vec3 normal;
			fscanf_s(file, "%f %f %f\n", &normal.x, &normal.y, &normal.z);
			temp_normals.push_back(normal);
		}
		else if (strcmp(line_header, "f") == 0)
		{
			// face
			std::string vertex1, vertex2, vertex3;
			unsigned int vertex_index[3], normal_index[3];
			int matches = fscanf_s(file, "%d//%d %d//%d %d//%d\n", &vertex_index[0], &normal_index[0], &vertex_index[1], &normal_index[1], &vertex_index[2], &normal_index[2]);
			if (matches != 6) {
				printf("file can't be read.\n");
				return false;
			}
			vertex_indices.push_back(vertex_index[0]);
			vertex_indices.push_back(vertex_index[1]);
			vertex_indices.push_back(vertex_index[2]);
			normal_indices.push_back(normal_index[0]);
			normal_indices.push_back(normal_index[1]);
			normal_indices.push_back(normal_index[2]);

			if (color_index == -1) color_index++;

			color_indices.push_back(color_index);
			color_indices.push_back(color_index);
			color_indices.push_back(color_index);
		}
		else if (strcmp(line_header, "usemtl") == 0)
		{
			// using a new material/color
			color_index++;
			printf("new material found in obj file");
		}
	}

	printf("processing object file data...\n");
	// process data
	for (unsigned int i = 0; i < vertex_indices.size(); i++)
	{
		unsigned int vertex_index = vertex_indices[i];
		glm::vec3 vertex = temp_vertices[vertex_index - 1];
		out_vertices.push_back(vertex);
	}
	for (unsigned int i = 0; i < normal_indices.size(); i++)
	{
		unsigned int normal_index = normal_indices[i];
		glm::vec3 normal = temp_normals[normal_index - 1];
		out_normals.push_back(normal);
	}

	// read the mtl file
	printf("done loading object file %s\nloading mtl file %s\n", obj_path, mtl_path);
	fopen_s(&file, mtl_path, "r");
	color_index = -1;
	if (file == NULL)
	{
		printf("could not open mtl file!\n");
		return false;
	}
	while (true)
	{
		char line_header[128];

		// read first word of line
		int res = fscanf_s(file, "%s", line_header, 128);
		if (res == EOF)
		{
			printf("EOF found\n");
			break;
		}
		if (strcmp(line_header, "newmtl") == 0)
		{
			color_index++;
			printf("color index: %i\n", color_index);
		}
		else if (strcmp(line_header, "Kd") == 0)
		{
			glm::vec3 color;
			fscanf_s(file, "%f %f %f", &color.x, &color.y, &color.z);
			temp_colors.push_back(color);
			printf("color data read from mtl. R: %f G: %f B: %f\n", color.x, color.y, color.z);
		}
	}

	printf("processing mtl data..\n");

	// process data
	for (unsigned int i = 0; i < color_indices.size(); i++)
	{
		color_index = color_indices[i];
		glm::vec3 color = temp_colors[color_index];
		out_colors.push_back(color);
	}

	printf("done loading mtl file %s\n", mtl_path);

	return true;
}

// function to close the window when the ESC key is pressed
static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		glfwSetWindowShouldClose(window, GLFW_TRUE);
}

// error callback function, prints out the error message to stderr
void error_callback(int error, const char* description)
{
	fprintf(stderr, "Error: %s\n", description);
}

bool init()
{
	// make GLFW call error_callback() whenever ther's an error
	glfwSetErrorCallback(error_callback);

	// attemt to initialize GLFW
	if (glfwInit())
	{
		// set the OpenGL version to 4.6
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);

		glfwWindowHint(GLFW_SAMPLES, 4);

		// attempt to create the window
		window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "GLFW-based 3D game", NULL, NULL);
		if (window != NULL)
		{
			glfwMakeContextCurrent(window);

			// attemt to load advanced OpenGL functions using GLAD
			if (gladLoadGL())
			{
				glfwSetKeyCallback(window, key_callback);

				// enable Vsync
				glfwSwapInterval(1);

				glEnable(GL_DEPTH_TEST);
				glDepthFunc(GL_LESS);

				glEnable(GL_CULL_FACE);
				glFrontFace(GL_CCW);

				glEnable(GL_MULTISAMPLE);

				return true;
			}
		}
	}
	return false;
}

void quit()
{
	glfwDestroyWindow(window);
	glfwTerminate();
}