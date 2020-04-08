#include <internal/terrain_generation.h>
#include <internal/spline.h> // won't compile if this is in the header?
// won't take define from header file? fix this
//#define max(a, b) a > b ? a : b
//#define min(a, b) a < b ? a : b

double random()
{
	return ((double) (rand() % 1000)) / 1000.0;
}

int round_down(int n, int m)
{
	return n - (n % m);
}

/*
	  __________
y2  v2|	       |v4
 	  |        |
	  |        |
y1  v1|________|v3
     
	 x1		   x2
0, 0
*/

double bilinear_interpolation(double v1, double v2, double v3, double v4, double x1, double x2, double y1, double y2, double x, double y)
{
	double x2x1, y2y1, x2x, y2y, yy1, xx1;
	x2x1 = x2 - x1;
	y2y1 = y2 - y1;
	x2x = x2 - x;
	y2y = y2 - y;
	yy1 = y - y1;
	xx1 = x - x1;
	return 1.0 / (x2x1 * y2y1) * (
		v1 * x2x * y2y +
		v3 * xx1 * y2y +
		v2 * x2x * yy1 +
		v4 * xx1 * yy1
		);
}

std::vector<std::vector<double>> bicubic_interpolation(std::vector<std::vector<double>> h, int gap) 
{
	int size = h.size() * gap;

	// output vector
	std::vector<std::vector<double>> out(size, std::vector<double>(size, INIT_VALUE));

	// spline to calculate interpolation
	tk::spline s;

	// array of positions of known points
	std::vector<double> pos_arr;
	for (int i = 0; i < size; i += gap) 
		pos_arr.push_back(i);

	// interpolate columns
	for (unsigned int x = 0; x < h.size(); x++)
	{
		s.set_points(pos_arr, h[x]);

		for (int y = 0; y < size; y++)
		{
			out[x*gap][y] = s(y);
		}
	}
	
	// interpolate rows
	for (int y = 0; y < size; y++)
	{
		// separate vector to store the points in the rows correctly
		std::vector<double> row;
		for (int i = 0; i < size; i += gap)
		{
			row.push_back(out[i][y]);
		}

		s.set_points(pos_arr, row);

		for (int x = 0; x < size; x++)
		{
			out[x][y] = s(x);
		}
	}

	return out;
}

void noise(int size, std::vector<std::vector<double>>& map, double amplitude, int frequency)
{
	time_t start_time = time(0);

	int s = size / frequency;

	// vector of vectors of doubles to store the noise
	std::vector<std::vector<double>> temp(s, std::vector<double>(s, INIT_VALUE));

	// generate the noise
	for (int i = 0; i < s; i++)
	{
		for (int j = 0; j < s; j++)
		{
			temp[i][j] = random() * amplitude;
		}
	}

	// interpolation
	if (frequency != 1)
	{
		if (frequency <= pow(2, log2(size)/3))
		{
			// bilinear interpolation for high frequencies (faster but less accurate)
			std::vector<std::vector<double>> t(size, std::vector<double>(size, INIT_VALUE));
			for (int x = 0; x < size; x++)
				for (int y = 0; y < size; y++)
					t[x][y] = bilinear_interpolation(                                                                                                   //        ____
						temp[(int)(x / frequency)                                       ][(int)(y / frequency)                                       ], // v1  y2|    |
						temp[(int)(x / frequency)                                       ][(int)(y / frequency) < s - 1 ? (int)(y / frequency) + 1 : 0], // v2    |    |
						temp[(int)(x / frequency) < s - 1 ? (int)(x / frequency) + 1 : 0][(int)(y / frequency)                                       ], // v3  y1|____|
						temp[(int)(x / frequency) < s - 1 ? (int)(x / frequency) + 1 : 0][(int)(y / frequency) < s - 1 ? (int)(y / frequency) + 1 : 0], // v4    x1  x2
						round_down(x, frequency),
						round_down(x, frequency) + frequency,
						round_down(y, frequency),
						round_down(y, frequency) + frequency,
						x,
						y
						);
			temp = t;
		}
		else
			// bicubic interpolation for low frequencies (slower but more accurate)
			temp = bicubic_interpolation(temp, frequency);
	}

	// add onto existing map
	for (int i = 0; i < size; i++)
	{
		for (int j = 0; j < size; j++)
		{
			map[i][j] += temp[i][j];
		}
	}

}

void generate_terrain(int size, int iterations, double amplitude, std::vector<std::vector<glm::vec3>>& vertices, std::vector<glm::vec3>& colors, std::vector<glm::vec3>& normals, int& completion)
{
	// seed the random number generator
	srand((unsigned int) time(0));	

	// output map
	std::vector<std::vector<double>> map(size, std::vector<double>(size, INIT_VALUE));

	// if iterations is 0, make the terrain generation run until it hits the smallest frequency
	if (iterations == 0) iterations = size;
	
	// timing system
	time_t start_time = time(0);
	time_t step_time = time(0);

	// generate the map

	//// parallel threads to improve performance and generation speed
	std::vector<std::thread> threads;
	for (int i = 0; i < iterations && pow(2, i) < size / 2; i++)
		// create one thread per layer of noise
		//                            func,  size, map,           amplitude,                frequency
		threads.push_back(std::thread(noise, size, std::ref(map), pow(2, i) * amplitude, pow(2, i)));

	// join the parallel threads
	int j = 1;
	for (std::vector<std::thread>::iterator i = threads.begin(); i != threads.end(); i++)
	{
		i->join();
		completion += (j++ / threads.size()) * 40;
	}

	//SimplexNoise noise = SimplexNoise();
	//for (int x = 0; x < size; x++)
	//{
	//	for (int y = 0; y < size; y++)
	//	{
	//		map[x][y] = (noise.fractal(iterations, (float) x / size, (float) y / size) + 1) * 50;
	//		completion = (x * size + y) * 40 / (size*size);
	//	}
	//}

	printf("noise generation complete in t <= %f sec\n", difftime(time(0), start_time));
	completion = 40;
	
	// resize the output vector and calculate minimum and average height
	vertices.resize(size);
	double min_height = size;
	double avg_height = 0;
	for (int i = 0; i < size; i++)
	{
		vertices[i].resize(size);
		for (int j = 0; j < size; j++)
		{
			min_height = min(map[i][j], min_height);
			avg_height += map[i][j];
		}
	}
	avg_height /= size * size;

<<<<<<< HEAD
	// calculate minimum height on map to ensure that all points are positive
	double min_height = size / 2;
	double max_height = 0;
	for (int i = 0; i < size; i++)
	{
		for (int j = 0; j < size; j++)
		{
			min_height = min(map[i][j], min_height);
			max_height = max(map[i][j], min_height);
		}
=======
	// island mask
	double max_width = (size / 4);
	int centers_x[3];
	int centers_y[3];
	int centers_weight[3] = { 100, 60, 60 };
	int weight = 0;

	// choose 3 random centers for the island
	// these are the highest points
	for (int i = 0; i < 3; i++)
	{
		centers_x[i] = random() * size/2;
		centers_y[i] = random() * size/2;
		weight += centers_weight[i];
>>>>>>> parent of 44e4dd6... New terrain genration somewhat works, parts are cutting off at the edges though
	}
	printf("max height %f", max_height);

	// island mask
	double max_width = size / 2;
	for (int x = 0; x < size; x++)
	{
		for (int y = 0; y < size; y++)
		{
<<<<<<< HEAD
			double dist = sqrt(pow(x - (size / 2), 2) + pow(y - (size / 2), 2));
			double factor = dist * max_height / max_width;
			 
			map[x][y] += min_height;
			map[x][y] = max(0, map[x][y] - factor);

			if (dist >= max_width - 32)
			{
				factor = (dist / (max_width - 32)) - 1;

				map[x][y] *= 1 - factor;
			}
		}
	}

	// calculate average and minimum heights on map
	vertices.resize(size);
	double avg_height = 0;
=======
			double dist = 0; 
			for (int i = 0; i < 3; i++)
			{
				dist += sqrt(pow(x - centers_x[i], 2) + pow(y - centers_y[i], 2)) * centers_weight[i];
			}
			dist /= weight;

			double factor = dist / max_width;

			factor *= factor;

			//map[x][y] -= avg_height - min_height;
			map[x][y] *= max(0, 1 - factor);
		}
	}

	// recalculate average and minimum heights on new map
	avg_height = 0;
>>>>>>> parent of 44e4dd6... New terrain genration somewhat works, parts are cutting off at the edges though
	min_height = size;
	for (int i = 0; i < size; i++)
	{
		for (int j = 0; j < size; j++)
		{
			if (isnan(map[i][j])) map[i][j] = 0;
			min_height = min(map[i][j], min_height);
			avg_height += map[i][j];
		}
	}
	avg_height /= size * size;

	double water_level = avg_height;

	printf("noise processing complete in t <= %f sec\n", difftime(time(0), step_time));
	time(&step_time);
	printf("terrain generation complete in t <= %f sec\n", difftime(time(0), start_time));
	completion = 50;

	// output to vector

	// use one tread per row of values in order to optimize for time and resources
	threads.resize(0);
	vertices.resize(size);
	for (int i = 0; i < size - 1; i++)
	{
		vertices[i].resize(size);
		for (int j = 0; j < size - 1; j++)
		{
			/*
			   ______
			v1 |\   | v3
			   | \  |
			   |  \ |
			v2 |___\| v4
			*/

			glm::vec3 vert1 = glm::vec3(i - size / 2, map[i][j], j - size / 2);
			glm::vec3 vert2 = glm::vec3(i + 1 - size / 2, map[i + 1][j], j - size / 2);
			glm::vec3 vert3 = glm::vec3(i - size / 2, map[i][j + 1], j + 1 - size / 2);
			glm::vec3 vert4 = glm::vec3(i + 1 - size / 2, map[i + 1][j + 1], j + 1 - size / 2);

			glm::vec3 u1 = vert1 - vert4;
			glm::vec3 v1 = vert1 - vert2;
			glm::vec3 normal1 = glm::vec3(
				(u1.y * v1.z) - (u1.z * v1.y),
				(u1.z * v1.x) - (u1.x * v1.z),
				(u1.x * v1.y) - (u1.y * v1.x));

			normals.push_back(normal1);
			normals.push_back(normal1);
			normals.push_back(normal1);

			glm::vec3 u2 = vert1 - vert3;
			glm::vec3 v2 = vert1 - vert4;
			glm::vec3 normal2 = glm::vec3(
				(u2.y * v2.z) - (u2.z * v2.y),
				(u2.z * v2.x) - (u2.x * v2.z),
				(u2.x * v2.y) - (u2.y * v2.x));

			normals.push_back(normal2);
			normals.push_back(normal2);
			normals.push_back(normal2);

			glm::vec3 grass(0.2, 1, 0.2);
			glm::vec3 sand(0.76, 0.7, 0.5);

			if (((vert1.y + vert2.y + vert4.y) / 3) - water_level < 2.5)
			{
				colors.push_back(sand);
				colors.push_back(sand);
				colors.push_back(sand);
			}
			else
			{
				colors.push_back(grass);
				colors.push_back(grass);
				colors.push_back(grass);
			}

			if (((vert1.y + vert3.y + vert4.y) / 3) - water_level < 2.5)
			{
				colors.push_back(sand);
				colors.push_back(sand);
				colors.push_back(sand);
			}
			else
			{
				colors.push_back(grass);
				colors.push_back(grass);
				colors.push_back(grass);
			}

			vertices[i][j] = vert1;
			vertices[i + 1][j] = vert2;
			vertices[i][j + 1] = vert3;
			vertices[i + 1][j + 1] = vert4;
		}
		completion = 50 + (i * 50 / size);
	}

	std::vector<glm::vec3> water;

	water.push_back(glm::vec3(-size, water_level, -size));
	water.push_back(glm::vec3(-size, water_level, size));
	water.push_back(glm::vec3(size, water_level, -size));

	water.push_back(glm::vec3(size, water_level, -size));
	water.push_back(glm::vec3(-size, water_level, size));
	water.push_back(glm::vec3(size, water_level, size));

	vertices.push_back(water);

	colors.push_back(glm::vec3(0.2, 0.2, 1));
	colors.push_back(glm::vec3(0.2, 0.2, 1));
	colors.push_back(glm::vec3(0.2, 0.2, 1));

	colors.push_back(glm::vec3(0.2, 0.2, 1));
	colors.push_back(glm::vec3(0.2, 0.2, 1));
	colors.push_back(glm::vec3(0.2, 0.2, 1));

	normals.push_back(glm::vec3(0, 1, 0));
	normals.push_back(glm::vec3(0, 1, 0));
	normals.push_back(glm::vec3(0, 1, 0));

	normals.push_back(glm::vec3(0, 1, 0));
	normals.push_back(glm::vec3(0, 1, 0));
	normals.push_back(glm::vec3(0, 1, 0));

	printf("terrain output complete in t <= %f sec\n", difftime(time(0), step_time));
	printf("total time = %f sec\n", difftime(time(0), start_time));
	printf("water level: %f", water_level);
	completion = 100;
}
