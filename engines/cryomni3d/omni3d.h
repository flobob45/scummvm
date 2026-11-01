/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
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
 */

#ifndef CRYOMNI3D_OMNI3D_H
#define CRYOMNI3D_OMNI3D_H

#include "graphics/surface.h"

namespace CryOmni3D {

class Omni3DManager {
public:
	// Géométrie du rendu. Historiquement figée à 640x480 avec des carreaux de 16 px
	// (40x30), ces valeurs sont désormais nommées pour pouvoir être élargies —
	// objectif du fork : 848x480 (16:9) puis 1120x480 (21:9), la largeur devant
	// rester un multiple de kTileSize.
	static const int kTileSize = 16;
	static const int kDefaultWidth = 640;
	static const int kDefaultHeight = 480;
	// Largeur de rendu effectivement utilisée, le temps de mettre au point
	// l'élargissement : 640 = 4:3 d'origine, 848 = 16:9, 1120 = 21:9.
	// Doit rester un multiple de kTileSize et ne pas dépasser kMaxCols*taille.
	static const int kRenderWidth = 864;
	// capacité maximale des tableaux : 1120 px de large (21:9) et 480 de haut
	static const int kMaxCols = 1120 / kTileSize;    // 70 carreaux
	static const int kMaxRows = 480 / kTileSize;     // 30 carreaux
	// une ligne de nœuds compte kMaxCols+1 colonnes, deux int par nœud
	static const int kCoordsPerRow = (kMaxCols + 1) * 2;

	Omni3DManager() : _vfov(0), _alpha(0), _beta(0), _xSpeed(0), _ySpeed(0), _alphaMin(0), _alphaMax(0),
		_betaMin(0), _betaMax(0), _helperValue(0), _width(kDefaultWidth), _height(kDefaultHeight),
		_cols(kDefaultWidth / kTileSize), _rows(kDefaultHeight / kTileSize),
		_dirty(true), _dirtyCoords(true),
		_sourceSurface(nullptr) {}
	virtual ~Omni3DManager();

	void init(double hfov);

	void setSourceSurface(const Graphics::Surface *surface) { _sourceSurface = surface; _dirty = true; }

	void clearConstraints();
	void setAlphaConstraints(double alphaMin, double alphaMax) { _alphaMin = alphaMin; _alphaMax = alphaMax; }
	void setBetaMinConstraint(double betaMin) { _betaMin = betaMin; }
	void setBetaMaxConstraint(double betaMax) { _betaMax = betaMax; }

	void setAlpha(double alpha) { _alpha = alpha; _dirtyCoords = true; }
	void setBeta(double beta) { _beta = beta; _dirtyCoords = true; }
	void updateCoords(int xDelta, int yDelta, bool useOldSpeed);

	double getAlpha() const { return _alpha; }
	double getBeta() const { return _beta; }

	Common::Point mapMouseCoords(const Common::Point &mouse);

	bool hasSpeed() { return _xSpeed != 0. || _ySpeed != 0.; }
	bool needsUpdate() { return _dirty || _dirtyCoords; }
	const Graphics::Surface *getSurface();

private:
	void updateImageCoords();

	double _vfov;

	double _alpha, _beta;
	double _xSpeed, _ySpeed;

	double _alphaMin, _alphaMax;
	double _betaMin, _betaMax;

	int _imageCoords[(kMaxRows + 1) * kCoordsPerRow];
	// _anglesH décrit les nœuds VERTICAUX (un par ligne de carreaux, +1),
	// _oppositeV les nœuds HORIZONTAUX de la moitié gauche et du centre seulement :
	// la moitié droite est remplie par symétrie (cf. l'offset de updateImageCoords).
	double _squaresCoords[kMaxRows + 1][kMaxCols / 2 + 1];
	double _hypothenusesH[kMaxRows + 1];
	double _anglesH[kMaxRows + 1];
	double _oppositeV[kMaxCols / 2 + 1];
	double _helperValue;

	int _width, _height;   // surface rendue
	int _cols, _rows;      // carreaux

	bool _dirty;
	bool _dirtyCoords;
	const Graphics::Surface *_sourceSurface;
	Graphics::Surface _surface;
};

} // End of namespace CryOmni3D

#endif
