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

// Outils de mesure du chantier Versailles HD : dump de la projection et trace
// de la jouabilité.
//
// DÉSACTIVÉ pour une vraie partie : le bloc force alpha = beta = 0 au premier
// affichage et réécrit un PGM de 300 Ko + le tableau de projection À CHAQUE
// image (~18 Mo/s), ce qui fausse le rendu et la fluidité.
// À RÉACTIVER pour rejouer les preuves de non-régression (dump-omni3d.sh,
// verifier-jouabilite.py) et la cartographie des zones.
// #define DUMP_OMNI3D

// Trace verbeuse du survol (une ligne par déplacement de souris, plus les zones
// de chaque pièce). Utile pour éprouver la conversion des clics, mais elle noie
// le journal en jeu normal : à réactiver pour un test seulement.
// #define TRACE_JOUEABILITE

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
	// Dimensions par défaut du panorama SOURCE (les fichiers HLZ du jeu). Ce sont
	// des valeurs de repli : la taille réelle est lue sur la surface chargée, car
	// le dossier de surcharge HD fournit des panoramas agrandis (×4 : 8192×3072).
	// À ne pas confondre avec la surface rendue : le centre vertical vaut
	// srcHeight/2 = 384, pas kDefaultHeight/2 = 240 — confondre les deux décale
	// tout le rendu de 144 px.
	static const int kDefaultSrcWidth = 2048;
	static const int kDefaultSrcHeight = 768;
	// Cible par défaut : 1920x1088, soit du 16:9 (1080 n'est pas un multiple de 16 :
	// 68 carreaux = 1088). La source (8192x3072 sur 360x155 degrés) est déjà à
	// 1:1 à cette taille — au-delà, on interpole sans gagner de détail. La
	// montée vers l'écran natif est faite par le backend, pas ici.
	// Ce sont des VALEURS DE REPLI : init() reçoit la taille réelle choisie au
	// démarrage selon l'écran (1920x1088 ou 2560x1088). Largeur et hauteur
	// doivent rester des multiples de kTileSize ; la largeur doit en outre être
	// un multiple de 32 (symétrie de _oppositeV).
	static const int kDefaultRenderWidth = 1920;
	static const int kDefaultRenderHeight = 1088;
	// capacité maximale des tableaux : 2560 px de large (21:9) et 1088 de haut
	static const int kMaxCols = 2560 / kTileSize;    // 160 carreaux
	static const int kMaxRows = 1088 / kTileSize;    // 68 carreaux
	// une ligne de nœuds compte kMaxCols+1 colonnes, deux int par nœud
	static const int kCoordsPerRow = (kMaxCols + 1) * 2;

	Omni3DManager() : _vfov(0), _alpha(0), _beta(0), _xSpeed(0), _ySpeed(0), _alphaMin(0), _alphaMax(0),
		_betaMin(0), _betaMax(0), _helperValue(0), _width(kDefaultWidth), _height(kDefaultHeight),
		_cols(kDefaultWidth / kTileSize), _rows(kDefaultHeight / kTileSize),
		_srcWidth(kDefaultSrcWidth), _srcHeight(kDefaultSrcHeight),
		_dirty(true), _dirtyCoords(true),
		_sourceSurface(nullptr) {}
	virtual ~Omni3DManager();

	void init(double hfov);
	/** Initialise le rendu à la taille demandée, qui doit respecter les contraintes
	 *  de la grille ; à défaut, la valeur par défaut de cet axe est conservée. */
	void init(int width, int height, double hfov);
	void recalculerGeometrie();

	void setSourceSurface(const Graphics::Surface *surface);

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
	double _hfov;   // conservé pour recalculer la géométrie si la source change

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
	int _srcWidth, _srcHeight;  // dimensions du panorama source effectivement chargé

	bool _dirty;
	bool _dirtyCoords;
	const Graphics::Surface *_sourceSurface;
	Graphics::Surface _surface;
};

} // End of namespace CryOmni3D

#endif
