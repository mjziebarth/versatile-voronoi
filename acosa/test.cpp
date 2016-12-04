/* ACOSA command line test suite.
 * Copyright (C) 2016 Malte Ziebarth
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <vdtesselation.hpp>
#include <convexhull.hpp>
#include <order_parameter.hpp>

#include <random>
#include <math.h>
#include <iostream>
#include <limits>
#include <unistd.h>
#include <cstdlib>
#include <list>


const size_t N = 1000000;

/* This method obtains a random seed for testing. Adjust this method
 * if you want to reproduce a test run. */
static long random_seed(){
//	return 5186709571096577860; // An example test run that produced an error.
	
	std::random_device rd;
    std::uniform_int_distribution<long>
		dist(std::numeric_limits<long>::min(),
		     std::numeric_limits<long>::max());
    
    
    return dist(rd);
}



struct configuration {
	size_t N;
	size_t runs;
	bool   test_order_param;
	size_t r;
	bool r_selected;
};


static configuration get_config(int argc, char **argv){
	configuration conf = {0,  1, false, 0, false};
	
	char *Nvalue = nullptr;
	char *Rvalue = nullptr;
	char *rvalue = nullptr;
	int index;
	int c;

	opterr = 0;

	while ((c = getopt (argc, argv, "R:ON:r:")) != -1){
		switch (c)
		{
			case 'r':
				rvalue = optarg;
				std::cout << "rvalue: '" << rvalue << "'\n";
				break;
			case 'R':
				Rvalue = optarg;
				std::cout << "Rvalue: '" << Rvalue << "'\n";
				break;
			case 'O':
				conf.test_order_param = true;
				std::cout << "Testing order parameter!\n";
				break;
			case 'N':
				Nvalue = optarg;
				std::cout << "Nvalue: '" << Nvalue << "'\n";
				break;
			case '?':
				if (optopt == 'c')
					std::cerr << "Option -" << optopt
					          << " requires an argument.\n";
				else if (isprint(optopt))
					std::cerr << "Unknown option '-" << optopt << ".\n";
				else
					std::cerr << "Unknown option character '"
							  << (char)optopt  << "'\n";
			default:
				return {0, false};
		}
	}
	if (Nvalue){
		conf.N = std::atol(Nvalue);
	}
	if (Rvalue){
		conf.runs = std::atol(Rvalue);
	}
	if (rvalue){
		conf.r_selected = true;
		conf.r = std::atol(rvalue);
	}
	return conf;
}


/*!
 * This method tests the OrderParameter class. It starts by creating a
 * list of two OrderParameters: max() and min()
 * Then, it successively inserts new OrderParameter in the ordered list
 * by randomly selecting two existing neighbours of the list,
 * calculating their mean, and inserting the mean into the list.
 * 
 * At each step, the order left < mean < right is checked.
 */
void test_order_parameter(size_t N) {
	/* RNG to calculate insert positions: */
	std::mt19937_64 engine(random_seed());
	std::uniform_real_distribution<double> generator;
	
	/* Ordered list of parameters: */
	std::list<ACOSA::OrderParameter> order_params;
	order_params.push_back(ACOSA::OrderParameter::max());
	order_params.push_front(ACOSA::OrderParameter::min());
	for (size_t i=2; i<N; ++i){
		/* Calculate the insert position, alway between the first and
		 * last element: */
		size_t pos = generator(engine) * i; 
		pos = std::min(std::max(pos, (size_t)1), i-1);
		auto it = order_params.begin();
		for (size_t j=0; j<pos; ++j){
			++it;
		}
		--it;
		ACOSA::OrderParameter left = *it;
		++it;
		ACOSA::OrderParameter right = *it;
		ACOSA::OrderParameter middle 
			= ACOSA::OrderParameter::between(left, right);
		if (!(middle > left && middle < right)){
			std::cerr <<   "ERROR in ordering!\n\tleft:  "
			          << left.to_string() << "\n\tright: "
			          << right.to_string()<< "\n\tmid:   "
			          << middle.to_string() << "\n";
			throw 0;
		}
		order_params.insert(it, middle);
	}
}





/*!
 * This compiles into a small testing application. The application
 * creates a number of sets of randomly distributed points on a unit
 * sphere and calculates their VDTesselation, the Voronoi network,
 * the Voronoi cell areas, the Delaunay network, and a convex hull using
 * a random inside direction (this is not yet really sensible since
 * for a randomly distributed network it's hard to define the
 * "outside").
 * 
 * The application can be called from the command line using the
 * following parameters:
 * "-N x" : Defines N := x, the size of each of the random set of nodes.
 *          Needs to be supplied as there is no default.
 * "-R x" : Defines R := x, the number of runs or number of different
 *          sets generated.
 * "-r x" : Selects r := x, the number of the first executed run (all
 *          other runs are empty generated nodes. This is a feature to
 *          select a known bad run for a certain seed for debugging).
 * "-O"   : A different test mode is chosen where the OrderParameter
 *          class is tested.
 */
int main(int argc, char **argv){
	/* Parse command line argument: */
	configuration c = get_config(argc, argv);
	
	ACOSA::OrderParameter::hist.clear();
	
	if (c.test_order_param){
		test_order_parameter(c.N);
		return 0;
	}
	
	
	size_t N = c.N;
	if (!N){
		std::cerr << "N==0, returning!\n";
		return -1;
	}
	
	/* Initialize random number generator: */
	long seed = random_seed();
	std::cout << "Create random nodes. (seed=" << seed << ")\n";
	std::mt19937_64 engine(random_seed());
	std::uniform_real_distribution<double> generator;
	
	for (size_t r=0; r<c.runs; ++r){
		std::cout << "run " << r << "/" << c.runs << "\n";
		
		/* Create random nodes: */
		std::vector<ACOSA::Node> nodes(N);
		for (size_t i=0; i<N; ++i){
			nodes[i] = ACOSA::Node(2*M_PI*generator(engine),
								   M_PI*(0.5-generator(engine)));
		}
		
		if (c.r_selected && r != c.r){
			/* If we want a certain run, fast forward: */
			std::cout << "  --> skipping.\n";
			continue;
		}
		
		/* Create tesselation: */
		std::cout << "Create tesselation.\n";
		ACOSA::VDTesselation tesselation(nodes);
		
		
		/* Obtain network: */
		std::cout << "Obtain Voronoi network.\n";
		std::vector<ACOSA::Node> voronoi_nodes;
		std::vector<ACOSA::Link> voronoi_links;
		tesselation.voronoi_tesselation(voronoi_nodes, voronoi_links);
		voronoi_nodes.clear();
		voronoi_links.clear();
		
		/* Obtain areas: */
		std::cout << "Obtain Voronoi areas.\n";
		std::vector<double> weights;
		tesselation.voronoi_cell_areas(weights);
		
		/* Obtain Delaunay tesselation: */
		std::cout << "Obtain Delaunay tesselation.\n";
		std::vector<ACOSA::Link> delaunay_links;
		tesselation.delaunay_triangulation(delaunay_links);
		
		/* Obtain hull: */
		std::cout << "Obtain hull.\n";
		ACOSA::Node inside = ACOSA::Node(2*M_PI*generator(engine),
										 M_PI*(0.5-generator(engine)));
		ACOSA::ConvexHull hull(nodes, inside);
	
	}
		
	/* Informative output: */
	std::cout << "Histogram of OrderParameter lengths:\n";
	for (size_t i=0; i<ACOSA::OrderParameter::hist.size(); ++i){
		std::cout << "\t[ " << i << ": " << ACOSA::OrderParameter::hist[i] 
				  << " ]\n";
	}
	std::cout << "\n";
	
	
	std::cout << "Finished.\n";
	return 0;
}
