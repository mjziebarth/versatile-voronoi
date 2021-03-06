/* Implementation of the spherical Fortune's algorithm of [1]. Part of
 * ACOSA.
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




#ifndef ACOSA_FORTUNES_SPHERE_HPP
#define ACOSA_FORTUNES_SPHERE_HPP

#include <basic_types.hpp>
#include <vector>

namespace ACOSA {

/* Methods: */


/*!
 * \brief Calculates the Delaunay triangulation of a set of nodes on
 *        a sphere.
 * \param nodes Vector of node coordinates. Should not contain any
 *              duplicates (within tolerance). Otherwise, an
 *              std::domain_error is thrown.
 * \param delaunay_triangles Output vector of Delaunay triangles.
 * \param tolerance A tolerance to account for numerical errors when
 *                  calculating the geometric points. This parameter
 *                  is crucial especially for regular grids, where
 *                  rounding error may lead to chaotic behaviour
 *                  if tolerance is too low.
 *                  Random node sets are, empirically more resistant
 *                  to lower tolerance
 * 
 * The code is an implementation of the plane sweep Voronoi algorithm
 * described in [1]. It has complexity O(N*log(N)).
 *
 * The tolerance parameter is used when determining whether a circle
 * event is above or below the sweepline. If no tolerance is used,
 * numerical errors may falsify the results, especially for regular
 * grids where circle events may coincide with the sweepline.
 * */
void delaunay_triangulation_sphere(const std::vector<Node>& nodes,
    std::vector<Triangle>& delaunay_triangles, double tolerance);

} // NAMESPACE ACOSA

#endif // VORONOI_HPP
