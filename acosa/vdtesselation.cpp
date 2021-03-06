﻿/* ACOSA class computing the Voronoi tesselation and Delaunay
 * triangulation of a set of nodes on a sphere, using the Fortune's
 * variant in [1].
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
 * 
 * 
 * Bibliography:
 * [1] Xiaoyu Zheng et al.: A Plane Sweep Algorithm for the Voronoi 
 *     Tesselation of the Sphere, in: electronic-Liquid Crystal
 *     Communications, 2011-12-13
 *     http://www.e-lc.org/docs/2011_12_05_14_35_11
 */

#include <vdtesselation.hpp>
#include <spherics.hpp>
#include <fortunes_sphere.hpp>
#include <geometricgraph.hpp>

#include <map>
#include <set>
#include <unordered_map>
#include <iostream>
#include <math.h>
#include <algorithm>

namespace ACOSA {

/* Cache states: */
static constexpr unsigned char DELAUNAY_LINKS_CACHED = 1;
static constexpr unsigned char VORONOI_NODES_CACHED =  2;
static constexpr unsigned char VORONOI_LINKS_CACHED =  4;
static constexpr unsigned char VORONOI_CELLS_CACHED =  8;
static constexpr unsigned char DUAL_LINKS_CACHED    = 16;

static constexpr unsigned char ALL_CACHED = 0xFF;


static void
delaunay_triangulation_brute_force(const std::vector<Node>& nodes,
                                   std::vector<Triangle>& triangles,
                                   double tolerance)
{
	const size_t N = nodes.size();

	/* Create internal data structure for nodes: */
	std::vector<SphereVectorEuclid> v(N);
	for (size_t i=0; i<N; ++i){
		v[i] = SphereVectorEuclid(nodes[i].lon, nodes[i].lat);
	}

	for (size_t i=0; i<N; ++i){
		for (size_t j=i+1; j<N; ++j){
			for (size_t k=j+1; k<N; ++k){
				/* Step 1: Check triangle (i,j,k) */
				SphereVectorEuclid cc =
				    SphereVectorEuclid::circumcenter(v[i], v[j], v[k]);
				double max_distance =
				        (cc.distance(v[i]) + cc.distance(v[j])
				         + cc.distance(v[k])) / 3.0
				        - tolerance;
				bool is_delaunay = true;
				for (size_t m=0; m<N; ++m){
					if (m != i && m != j && m != k &&
					    cc.distance(v[m]) <= max_distance)
					{
						is_delaunay = false;
					}
				}
				if (is_delaunay){
					triangles.emplace_back(i,j,k);
				}
				/* Step 2: Check triangle (i,k,j)
				 *         (the antipodal one) */
				cc = SphereVectorEuclid::circumcenter(v[i], v[k],
				                                      v[j]);
				max_distance =
				        (cc.distance(v[i]) + cc.distance(v[j])
				         + cc.distance(v[k])) / 3.0
				        - tolerance;
				is_delaunay = true;
				for (size_t m=0; m<N; ++m){
					if (m != i && m != j && m != k &&
					    cc.distance(v[m]) <= max_distance)
					{
						is_delaunay = false;
					}
				}
				if (is_delaunay){
					triangles.emplace_back(i,k,j);
				}
			}
		}
	}
}

//------------------------------------------------------------------------------

static void tesselation_N3(const Node& n1, const Node& n2, const Node& n3,
                           std::vector<Triangle>& delaunay_triangles,
                           std::vector<size_t>& delaunay2voronoi,
                           std::vector<std::forward_list<size_t>>& voronoi2delaunay,
                           std::vector<Link>& delaunay_links,
                           std::vector<size_t>& dual_link_delaunay2voronoi,
                           std::vector<Node>& voronoi_nodes,
                           std::vector<double>& voronoi_areas)
{
	/* Delaunay triangulation: */
	delaunay_triangles.push_back({0,1,2});
	delaunay_triangles.push_back({0,2,1});
	delaunay_links.push_back({0,1});
	delaunay_links.push_back({0,2});
	delaunay_links.push_back({1,2});

	/* Voronoi nodes: */
	SphereVectorEuclid v1(n1), v2(n2), v3(n3);
	voronoi_nodes.push_back(SphereVectorEuclid::circumcenter(v1, v2, v3));
	voronoi_nodes.push_back(SphereVectorEuclid::circumcenter(v1, v3, v2));

	/* Maps between Voronoi and Delaunay: */
	delaunay2voronoi.push_back(0);
	delaunay2voronoi.push_back(1);
	voronoi2delaunay.resize(2);
	voronoi2delaunay[0].push_front(0);
	voronoi2delaunay[1].push_front(1);
	dual_link_delaunay2voronoi.resize(delaunay_links.size(), ACOSA::NO_LINK);

	/* Voronoi areas: Calculate longitude of nodes in a coordinate system */
	SphereVectorEuclid axis(voronoi_nodes[0]);
	SphereVector s1 = axis.cross(v1);
	SphereVector s2 = axis.cross(v2);
	SphereVector s3 = axis.cross(v3);

	/* Now calculate slices of the sphere that each of the three nodes
	 * conquers: */
	double d1 = s1.distance(s2);
	double d2 = s1.distance(s3);
	double d3 = s2.distance(s3);
	/* The longest distance belongs to the slice that is the other way round
	 * the globe: */
	if (d1 > d2){
		if (d1 > d3){
			d1 = 2*M_PI - d2 - d3;
		} else {
			d3 = 2*M_PI - d1 - d2;
		}
	} else {
		if (d2 > d3){
			d2 = 2*M_PI - d1 - d3;
		} else {
			d3 = 2*M_PI - d1 - d2;
		}
	}
	voronoi_areas.resize(3);
	voronoi_areas[0] = d1+d2;
	voronoi_areas[1] = d1+d3;
	voronoi_areas[2] = d2+d3;
}



//------------------------------------------------------------------------------
VDTesselation::VDTesselation(const std::vector<Node>& nodes,
							 double tolerance,
							 delaunay_algorithm_t algorithm,
							 int checks, bool on_error_display_nodes)
    : nodes(nodes), cache_state(0), N(nodes.size()), tolerance(tolerance)
{
	/* Special cases: N <= 3: */
	if (N <= 3){
		if (N == 0)
		{
			/* Empty node sets: Do nothing. */
		}
		else if (N == 1)
		{
			/* No links in Delaunay triangulation, no nodes or links in
			 * Voronoi tesselation, but whole sphere belongs to node's
			 * Voronoi cell: */
			voronoi_areas.resize(1, 4*M_PI);
		}
		else if (N == 2)
		{
			/* No triangles in the Delaunay triangulation. We default to not
			 * setting up a Delaunay triangulation.
			 * Voronoi tesselation consists of one great-circle splitting the
			 * sphere between both nodes. Since we cannot uniquely identify
			 * Voronoi nodes, the Voronoi tesselation is left empty as well.
			 * Both nodes, however, have a Voronoi cell area of 2pi. */
			voronoi_areas.resize(2, 2*M_PI);
		}
		else
		{
			/* Case N==3 is a little bit more complex so we use a seperate
			 * function.
			 * Note that we cannot describe the Voronoi tesselation in the
			 * framework used in this class as it allows only one link between
			 * two Voronoi nodes. For a set of cocircular nodes, however, all
			 * links are between just two Voronoi nodes.
			 * Thus, we do not define the Voronoi network's links. */
			tesselation_N3(nodes[0], nodes[1], nodes[2], delaunay_triangles_,
			               delaunay2voronoi, voronoi2delaunay, delaunay_links,
			               dual_link_delaunay2voronoi, voronoi_nodes,
			               voronoi_areas);
		}
		/* All caches have been set up: */
		cache_state = ALL_CACHED;
	} else {
		try {
			if (algorithm == FORTUNES){
				/* Do Fortune's algorithm: */
				delaunay_triangulation_sphere(nodes, delaunay_triangles_,
				                              tolerance);

			} else if (algorithm == BRUTE_FORCE) {
				/* Do a brute force algorithm: */
				std::cout << "WARNING :\nBRUTE_FORCE algorithm is probably "
				             "broken on lattices that have more than three "
				             "nodes on a circumcircle (e.g. regular lattices)"
				             ".\n";
				delaunay_triangulation_brute_force(nodes, delaunay_triangles_,
				                                   tolerance);
			}

			/* Consistency checks: */

			if (checks & CHECK_DUAL_LINKS){
				/* If the Delaunay triangulation is inconsistent in a way that
				 * not for every link a dual link of the Voronoi tesselation can
				 * be found, this will raise an exception: */
				calculate_dual_links();
			}

			if (checks & CHECK_VORONOI_CELL_AREAS){
				/* Calculate Voronoi areas: */
				calculate_voronoi_cell_areas();

				/* Add up the cell areas: */
				double sum = 0.0;
				for (double d : voronoi_areas){
					sum += d;
				}

				/* Throw exception if we're more than 10*N times further away
				 * from 4pi than tolerance: */
				if (std::abs(sum - 4*M_PI) > 10.0*N*tolerance){
					throw std::runtime_error("Sum of Voronoi areas (" +
					                    std::to_string(sum) + ") is more than "
					                    "10*N times farther than tolerance "
					                    "away from 4pi=" +
					                    std::to_string(4*M_PI) + "!");
				}
			}
		} catch (const std::runtime_error& e){
			/* Add a little hint to the error message: */
			if (on_error_display_nodes){
				std::cerr << "ERROR in VDTesselation().\nNode set that caused "
				             "the error:\n";
				std::cerr.precision(std::numeric_limits<double>::digits10);
				for (const Node& n : nodes){
					std::cerr << "\t(" << n.lon << "," << n.lat << ")\n";
				}
				std::cerr << "\n";
			}

			throw std::runtime_error("VDTesselation failed:\n\""
			                         + std::string(e.what()) +
			                         "\"\n\nHint: Changing tolerance "
			                         "or inverting the latitude coordinates "
			                         "may solve the problems encountered.\n");
		}
	}
}

//------------------------------------------------------------------------------
size_t VDTesselation::size() const
{
	return delaunay_triangles_.size();
}


//-----------------------------------------------------------------------------
void VDTesselation::delaunay_triangulation(std::vector<Link>& links)
	const
{
	/* Make sure Delaunay links are cached: */
	calculate_delaunay_links();
	
	/* Copy cache: */
	links = delaunay_links;
}

//------------------------------------------------------------------------------
const std::vector<Triangle>& VDTesselation::delaunay_triangles() const
{
	/* This is easy. */
	return delaunay_triangles_;
}

//------------------------------------------------------------------------------
void VDTesselation::voronoi_tesselation(std::vector<Node>& nodes,
	std::vector<Link>& links) const
{
	/* Make sure Voronoi network is cached: */
	calculate_voronoi_network();
	
	/* Copy cache: */
	nodes = voronoi_nodes;
	links = voronoi_links;
}


//------------------------------------------------------------------------------
void VDTesselation::voronoi_cell_areas(std::vector<double>& areas) const
{
	/* Make sure Voronoi areas are cached: */
	calculate_voronoi_cell_areas();
	
	/* Copy cache: */
	areas = voronoi_areas;
}



/* ************************** Caching ******************************* */


	
/* Refurbished Link-Class that orders its indices.
 * All links are undirected, so we don't need to count them twice
 * in different order (i,j), (j,i).
 * Instead, order them so that i<j. */
struct link_t {
	/* Constructor: */
	link_t(size_t i, size_t j){
		if (i < j){
			this->i = i;
			this->j = j;
		} else {
			this->i = j;
			this->j = i;
		}
	}
	
	/* Comparator: */
	bool operator<(const link_t& other) const {
		if (i == other.i){
			return j < other.j;
		}
		return i < other.i;
	}
	
	/* Members: */
	size_t i, j;
};



//------------------------------------------------------------------------------
void VDTesselation::calculate_delaunay_links() const
{
	/* Check if we've previously calculated the Delaunay links: */
	if (cache_state & DELAUNAY_LINKS_CACHED)
		return;
	
	/* We use a set to order the Delaunay links by their indices.
	 * This will also make sure that links are added only once (all
	 * links of the Delaunay triangulation are undirected, so we need
	 * only one of each pair (i,j) and (j,i)) */
	std::set<link_t> links;
	
	for (const Triangle& t : delaunay_triangles_){
		links.insert(link_t(t.i, t.j));
		links.insert(link_t(t.i, t.k));
		links.insert(link_t(t.j, t.k));
	}

	delaunay_links.reserve(links.size());
	for (const link_t& l : links){
		delaunay_links.emplace_back(l.i, l.j);
	}
	
	/* Update cache state: */
	cache_state |= DELAUNAY_LINKS_CACHED;
	tidy_up_cache();
}



//------------------------------------------------------------------------------
void VDTesselation::merge_clusters() const
{
	const size_t M = delaunay_triangles_.size();

	/* First, an injective map: */
	delaunay2voronoi.resize(M);
	for (size_t i=0; i<M; ++i){
		delaunay2voronoi[i] = i;
	}

	/* Now determine clustered Voronoi nodes (that actually belong to
	 * the same node): */

	std::vector<Link> cluster_links;
	geometric_graph_links(voronoi_nodes, cluster_links, tolerance);

	/* Sort the links: */
	std::sort<std::vector<Link>::iterator>(cluster_links.begin(),
	                                       cluster_links.end());


	/* Now merge clusters. To do this, we iterate over all links
	 * and set both nodes of each links to the same index (using
	 * the smaller index).
	 * Also adjust indices to fill the gaps in the index set. */
	size_t i_dest = 0;
	std::vector<bool> adjusted(M, false);
	std::vector<Node> merged_voronoi_nodes;
	size_t i_src=0;
	for (const Link& l : cluster_links){
		/* All links are given twice, once (i,j) and once (j,i).
		 * Process only the link where the first index is the smaller
		 * one: */
		if (l.i > l.j)
			continue;

		/* Iterate till we're at position l.i.
		 * Meanwhile, copy all */
		for (;i_src<l.i; ++i_src){
			if (!adjusted[i_src]){
				delaunay2voronoi[i_src] = i_dest++;
				merged_voronoi_nodes.push_back(voronoi_nodes[i_src]);
			}
		}

		/* Now proceed depending on whether the index has already
		 * been processed (that is the case if i<j): */
		if (!adjusted[i_src]){
			delaunay2voronoi[i_src] = i_dest++;
			merged_voronoi_nodes.push_back(voronoi_nodes[i_src]);
			adjusted[i_src] = true;
		}
		delaunay2voronoi[l.j] = delaunay2voronoi[i_src];
		adjusted[l.j] = true;

	}

	/* Adjust the remainder: */
	for (;i_src<M; ++i_src){
		if (!adjusted[i_src]){
			delaunay2voronoi[i_src] = i_dest++;
			merged_voronoi_nodes.push_back(voronoi_nodes[i_src]);
		}
	}

	/* Copy reduced Voronoi vector: */
	voronoi_nodes = merged_voronoi_nodes;

	/* Create an inverse map mapping Voronoi nodes to all contributing
	 * Delaunay triangles (needed for associated nodes): */
	voronoi2delaunay.resize(voronoi_nodes.size());
	for (size_t i=0; i<M; ++i){
		voronoi2delaunay[delaunay2voronoi[i]].push_front(i);
	}
}




//------------------------------------------------------------------------------
void VDTesselation::calculate_voronoi_nodes() const
{
	/* Check if we've previously calculated the Voronoi nodes: */
	if (cache_state & VORONOI_NODES_CACHED)
		return;
	
	/* Voronoi nodes are at the circumcenter of the three nodes of the
	 * Delaunay triangles: */
	voronoi_nodes.reserve(delaunay_triangles_.size());
	for (const Triangle& t : delaunay_triangles_){
		SphereVector vec = SphereVector::circumcenter(
						SphereVector(nodes[t.i].lon, nodes[t.i].lat),
						SphereVector(nodes[t.j].lon, nodes[t.j].lat),
						SphereVector(nodes[t.k].lon, nodes[t.k].lat));
		voronoi_nodes.emplace_back(vec.lon(), vec.lat());
	}

	/* Merge clusters if needed: */
	merge_clusters();
	
	/* Cache state: */
	cache_state |= VORONOI_NODES_CACHED;
	tidy_up_cache();
}


//------------------------------------------------------------------------------

static void voronoi_network_concyclic(const std::vector<Node>& nodes,
    std::vector<Node>& voronoi_nodes, std::vector<double>& voronoi_areas,
    double tolerance)
{
	const size_t N = nodes.size();

	/* Voronoi areas: Calculate longitude of nodes in a coordinate system
	 * given by z vector voronoi_nodes[0] and x vector the z-orthogonal part
	 * of nodes[0]: */
	SphereVectorEuclid ax_z(voronoi_nodes[0]);
	SphereVectorEuclid ax_x(nodes[0]);
	ax_x = (ax_x - (ax_x*ax_z)*ax_z);
	ax_x /= ax_x.norm();
	SphereVectorEuclid ax_y = ax_z.cross(ax_x);
	ax_y /= ax_y.norm(); // Precaution. Should be normed modulo rounding errors.
	struct indexed_lon_t{
		double val;
		size_t id;

		bool operator<(const indexed_lon_t& other) const {
			return val < other.val;
		}
	};
	std::vector<indexed_lon_t> lon(N);
	lon[0] = {0.0, 0};
	for (size_t i=1; i<N; ++i){
		/* Calculate coordinates in system given by ax_x and ax_z: */
		SphereVectorEuclid vec(nodes[i]);
		double x = vec*ax_x;
		double y = vec*ax_y;
		lon[i].val = std::atan2(y,x);
		if (lon[i].val < 0.0)
			lon[i].val += M_PI;
		lon[i].id = i;
	}

	/* Sort nodes by longitude in this coordinate system so that we have
	 * neighbouring Voronoi cells next to each other: */
	std::sort(lon.begin(), lon.end());

	/* Now we can calculate the Voronoi cell areas: */
	voronoi_areas.resize(N, 0.0);
	for (size_t i=0; i<N; ++i){
		double distance = lon[(i+1) % N].val - lon[i].val;
		if (distance < 0.0)
			/* May happen for i=N-1: */
			distance += 2*M_PI;
		voronoi_areas[i] += distance;
		voronoi_areas[(i+1) % N] += distance;
	}

}

//------------------------------------------------------------------------------
void VDTesselation::calculate_voronoi_network() const
{
	/* Check if we've previously calculated the Voronoi network: */
	if (cache_state & VORONOI_LINKS_CACHED)
		return;

	/* First make sure that Voronoi nodes are calculated: */
	calculate_voronoi_nodes();

	/* Handle the case that all nodes are concyclic (N>3) seperately: */
	if (voronoi_nodes.size() == 2){
		voronoi_network_concyclic(nodes, voronoi_nodes, voronoi_areas,
		                          tolerance);

		/* Cache state: */
		cache_state |= (VORONOI_LINKS_CACHED | VORONOI_CELLS_CACHED);
		tidy_up_cache();

		return;
	}

	/* Init Voronoi cell area vector: */
	voronoi_areas.resize(nodes.size(), 0.0);

	/* Each Delaunay triangle corresponds to a Voronoi node. In general,
	 * there will be a constant number of Voronoi nodes per Voronoi cell,
	 * so that we use indexing by node (of the input network) to generate
	 * the links of the Delaunay triangulation in linear time.
	 *
	 * Here, we reference, by node index, all triangles that node is
	 * a vertex of.
	 */
	std::vector<std::forward_list<size_t>> node2triangle(nodes.size());
	for (size_t i=0; i<delaunay_triangles_.size(); ++i){
		node2triangle[delaunay_triangles_[i].i].push_front(i);
		node2triangle[delaunay_triangles_[i].j].push_front(i);
		node2triangle[delaunay_triangles_[i].k].push_front(i);
	}



	/* Now we iterate over each node index (of the input network) and
	 * create a closed path, the Voronoi cell: */
	std::vector<size_t> path;

	struct helper_t {
		bool   available;
		size_t id;
		Triangle t;
	};

	std::vector<helper_t> local_triangles;
	for (size_t i=0; i<nodes.size(); ++i){
		/* Create a local copy of the set of triangles belonging to node
		 * i: */
		local_triangles.resize(0);
		for (size_t t : node2triangle[i]){
			local_triangles.push_back({true, t, delaunay_triangles_[t]});
		}
		node2triangle[i].clear();

		/* Find a closed path: */
		path.resize(0);
		path.push_back(local_triangles[0].id);
		Triangle last = local_triangles[0].t;
		for (size_t j=1; j<local_triangles.size(); ++j){
			bool found = false;
			for (size_t k=1; k<local_triangles.size(); ++k){
				if (!local_triangles[k].available)
					continue;
				if (last.common_border(local_triangles[k].t)){
					path.push_back(local_triangles[k].id);
					last = local_triangles[k].t;
					local_triangles[k].available = false;
					found = true;
					break;
				}
			}
		}

		/* Along this closed path, we can now calculate the cell areas
		 * of the Voronoi cells using adjacent triangles.
		 * Also, we can obtain links of the Voronoi tesselation.
		 * Since each link of the Voronoi tesselation is part of the Voronoi
		 * cell of two nodes, we will end up with each link being duplicate.*/
		double area = 0.0;
		size_t last_i = path[0];
		SphereVectorEuclid last_vec(voronoi_nodes[delaunay2voronoi[last_i]]);
		SphereVectorEuclid v_i(nodes[i]);
		for (size_t j=1; j<path.size(); ++j){
			size_t next_i = path[j];
			size_t l1 = delaunay2voronoi[next_i];
			size_t l2 = delaunay2voronoi[last_i];
			/* Because each Voronoi cell is a closed path with a fixed rotation,
			 * the following check should remove any duplicate points from the
			 * path (because they would be successive nodes). */
			if (l1 != l2){
				if (l1 < l2){
					voronoi_links.emplace_back(l1, l2);
				} else {
					voronoi_links.emplace_back(l2, l1);
				}
				SphereVectorEuclid next(voronoi_nodes[l1]);
				area += SphereVectorEuclid::triangle_area(last_vec, next,
				                                          v_i);
				last_vec = next;
			}
			last_i = next_i;
		}

		size_t l1 = delaunay2voronoi[path[0]];
		size_t l2 = delaunay2voronoi[last_i];
		if (l1 != l2){
			if (l1 < l2){
				voronoi_links.emplace_back(l1, l2);
			} else {
				voronoi_links.emplace_back(l2, l1);
			}
			area += SphereVectorEuclid::triangle_area(last_vec, v_i,
			                    SphereVectorEuclid(voronoi_nodes[l1]));

		}

		voronoi_areas[i] = area;
	}

	/* Now we have successfully created the Voronoi tesselation and
	 * calculated the Voronoi weights. Clean up:
	 */
	node2triangle.clear();
	local_triangles.clear();
	path.clear();

	/* Finally, we have to sort the Voronoi links... */
	std::sort<std::vector<Link>::iterator>
	        (voronoi_links.begin(), voronoi_links.end());


	/* ... and remove duplicates! */
	for (size_t i=1; i<voronoi_links.size()/2; ++i){
		voronoi_links[i] = voronoi_links[2*i];
	}

	voronoi_links.resize(voronoi_links.size()/2);

	/* Cache state: */
	cache_state |= (VORONOI_LINKS_CACHED | VORONOI_CELLS_CACHED);
	tidy_up_cache();
}

//------------------------------------------------------------------------------
void VDTesselation::calculate_voronoi_cell_areas() const
{
	/* Check if we've previously calculated the Voronoi cells: */
	if (cache_state & VORONOI_CELLS_CACHED)
		return;


	/* Calculation of Voronoi links makes calculating the weight easy: */
	calculate_voronoi_network();
}

//------------------------------------------------------------------------------

void VDTesselation::calculate_dual_links() const
{
	/* Early exit if cached: */
	if (cache_state & DUAL_LINKS_CACHED)
		return;

	/* Make sure we have calculated the Delaunay links and the Voronoi
	 * nodes: */
	calculate_delaunay_links();
	calculate_voronoi_network();

	/* Handle case where all nodes are concyclic: */
	if (voronoi_nodes.size() == 2){
		/* Since we cannot define the Voronoi edges in the framework used
		 * (which allows only at max one unique link between each Voronoi node
		 *  pair), we have to set the dual mapping to NO_LINK: */
		dual_link_delaunay2voronoi.resize(delaunay_triangles_.size(), NO_LINK);

		/* Update cache state: */
		cache_state |= DUAL_LINKS_CACHED;
		tidy_up_cache();

		return;
	}

	/* Create a map mapping all triangles a node is part of by the node's
	 * index: */
	std::vector<std::forward_list<size_t>> node2delaunay(N);
	for (size_t i=0; i<delaunay_triangles_.size(); ++i){
		const Triangle& t = delaunay_triangles_[i];
		node2delaunay[t.i].push_front(i);
		node2delaunay[t.j].push_front(i);
		node2delaunay[t.k].push_front(i);
	}

	/* Create a hashtable mapping Voronoi links to their indices in the
	 * voronoi_links vector: */
	std::unordered_map<Link,size_t> vlink2id;
	for (size_t i=0; i<voronoi_links.size(); ++i)
	{
		vlink2id.emplace(voronoi_links[i],i);
	}

	/* Now for each link (l,m) of the Delaunay triangulation find the two
	 * Delaunay triangles (n,o) which have the edge (l,m).
	 * (n,o) is the dual edge of (l,m). */
	dual_link_delaunay2voronoi.resize(delaunay_links.size());
	for (size_t p=0; p<delaunay_links.size(); ++p)
	{
		/* Find the dual link of the Delaunay link: */
		Link l = dual_link_d2v(delaunay_links[p], node2delaunay);

		/* If we have merged clusters (-> more than 3 cocircular nodes), there
		 * will be self links. In these cases set invalid index: */
		if (l.i == l.j){
			dual_link_delaunay2voronoi[p] = NO_LINK;
		} else {
			/* Now we need to find the link's index in
			 * voronoi_links: */
			std::unordered_map<Link,size_t>::const_iterator it = vlink2id.find(l);
			if (it == vlink2id.end())
				throw std::runtime_error("calculate_dual_links():\nLink ("
			                             + std::to_string(l.i) + "," +
			                             std::to_string(l.j) +
			                             ") not found in set of Voronoi links!");
			dual_link_delaunay2voronoi[p] = it->second;
		}
	}

	/* Update cache state: */
	cache_state |= DUAL_LINKS_CACHED;
	tidy_up_cache();
}

//------------------------------------------------------------------------------
Link VDTesselation::dual_link_d2v(const Link& link,
           const std::vector<std::forward_list<size_t> >& node2delaunay) const
{
	/* Find the Delaunay triangles which both nodes are part of (should be two):
	 */
	size_t tri[2];
	int k=0;
	for (size_t i : node2delaunay[link.i]){
		for (size_t j : node2delaunay[link.j]){
			if (i == j){
				tri[k] = i;
				++k;
				if (k == 2){
					/* We found the link. Construct the ordered version: */
					size_t m = delaunay2voronoi[tri[0]];
					size_t o = delaunay2voronoi[tri[1]];
					if (m < o)
						return Link(m,o);
					else
						return Link(o,m);
				 }
			}
		}
	}
	throw std::runtime_error("Dual edge not found!");
}


//------------------------------------------------------------------------------
void VDTesselation::associated_nodes(
	const std::vector<size_t>& voronoi_nodes,
	std::vector<size_t>& associated) const
{
	/* We iterate over all Voronoi nodes in the given set and mark
	 * the associated nodes of the original network.
	 * In the end, we collect all marked nodes. */
	std::vector<bool> marked(N, false);
	
	for (size_t node : voronoi_nodes){
		for (size_t triangle : voronoi2delaunay[node]){
			marked[delaunay_triangles_[triangle].i] = true;
			marked[delaunay_triangles_[triangle].j] = true;
			marked[delaunay_triangles_[triangle].k] = true;
		}
	}
	
	size_t count = 0;
	for (bool b : marked){
		if (b)
			++count;
	}
	
	associated.resize(count);
	size_t j=0;
	for (size_t i=0; i<N; ++i){
		if (marked[i])
			associated[j++] = i;
	}
}

//------------------------------------------------------------------------------
void VDTesselation::tidy_up_cache() const
{
	/* Check if nodes still need to be stored: */
	if ((cache_state & VORONOI_NODES_CACHED) &&
	    (cache_state & VORONOI_CELLS_CACHED))
	{
		nodes.clear();
	}
}

//------------------------------------------------------------------------------
void VDTesselation::print_debug(bool sort_triangles) const
{
	/* First print Delaunay tesselation: */
	std::cout << "--- VDTesselation debug output ---\n\nDelaunay tesselation:";
	size_t column = 0;

	/* Order triangles by smallest index first, then by j: */
	const size_t M = delaunay_triangles_.size();
	std::vector<Triangle> triangles_copy(M);
	for (size_t i=0; i<M; ++i){
		Triangle t = delaunay_triangles_[i];
		if (t.k < t.i && t.k < t.j){
			triangles_copy[i] = Triangle(t.k, t.i, t.j);
		} else if (t.j < t.i && t.j < t.k){
			triangles_copy[i] = Triangle(t.j, t.k, t.i);
		} else {
			triangles_copy[i] = t;
		}
	}
	std::vector<size_t> index_map(M);
	for (size_t i=0; i<M; ++i){
		index_map[i] = i;
	}
	if (sort_triangles){
		auto cmp = [&](size_t i, size_t j){
			    if (triangles_copy[i].i < triangles_copy[j].i){
					return true;
				} else if (triangles_copy[i].i > triangles_copy[j].i){
					return false;
				}
				if (triangles_copy[i].j < triangles_copy[j].j){
					return true;
				} else if (triangles_copy[i].j > triangles_copy[j].j){
					return false;
				}
				return triangles_copy[i].k < triangles_copy[j].k;
		    };

		std::sort<std::vector<size_t>::iterator,decltype(cmp)>(
		    index_map.begin(), index_map.end(), cmp);
	}



	std::cout << "[";
	for (size_t i=0; i<M; ++i){
		if (column == 0){
			std::cout << "\n  ";
		}
		Triangle t = triangles_copy[index_map[i]];
		std::cout << " [" << t.i << "," << t.j << "," << t.k << "]";
		if (i != M-1){
			std::cout << ",";
		}
		column = (column+1) % 1;
	}
	std::cout << "]\n";
	if (cache_state & VORONOI_NODES_CACHED){
		std::cout << "\nVoronoi nodes (in °):\n[";
		column = 0;
		for (size_t i=0; i<M; ++i){
			if (column == 0){
				std::cout << "\n  ";
			}

			std::cout << " [" << 180.0/M_PI*voronoi_nodes[index_map[i]].lon
			          << ","  << 180.0/M_PI*voronoi_nodes[index_map[i]].lat
			          << "]";
			if (i != M-1){
				std::cout << ",";
			}
			column = (column+1) % 3;
		}
		std::cout << "]\n";
	}
}


} // NAMESPACE ACOSA
