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

#include "cryomni3d/omni3d.h"

#include "common/file.h"
#include "common/rect.h"
#include "common/system.h"   // g_system, pour la mesure de fluidité
#include "common/str.h"
#include "common/textconsole.h"

namespace CryOmni3D {

void Omni3DManager::recalculerGeometrie() {
	// Le rapport hauteur/largeur dépend de la surface de rendu : 480/640 = 0,75
	// redonne exactement l'ancien (4/3) en dur, 1088/1920 donne le 16:9.
	double oppositeSide = tan(_hfov / 2.) * ((double)_height / _width);
	double vf = atan2(oppositeSide, 1.);
	_vfov = (M_PI_2 - vf - (13. / 180.*M_PI)) * 10. / 9.;

	double warpVfov = 155. / 180. * M_PI;
	double hypV = _srcHeight / 2. / sin(warpVfov / 2.);
	double oppHTot = tan(_hfov / 2.) * kTileSize / (_width / 2.);
	_helperValue = _srcWidth * 65536 / (2. * M_PI);

	for (int i = 0; i < _rows + 1; i++) {
		double oppH = (i - _rows / 2) * oppHTot;
		double angle = atan2(oppH, 1.);

		_anglesH[i] = angle;
		_hypothenusesH[i] = sqrt(oppH * oppH + 1);

		double oppVTot = hypV * _hypothenusesH[i];
		for (int j = 0; j < _cols / 2 + 1; j++) {
			double oppV = (j - _cols / 2) * oppHTot;

			_oppositeV[j] = oppV;

			double coord = sqrt(oppV * oppV + _hypothenusesH[i] * _hypothenusesH[i]);
			coord = oppVTot / coord;
			coord = coord * 65536;

			_squaresCoords[i][j] = coord;
		}
	}

}

void Omni3DManager::setSourceSurface(const Graphics::Surface *surface) {
	_sourceSurface = surface;
	if (surface) {
		// La taille du panorama fait foi : le dossier de surcharge HD fournit des
		// images agrandies (×4), et toute la projection en dépend.
		_srcWidth = surface->w;
		_srcHeight = surface->h;
		// La géométrie dépend de la taille de la source : la recalculer, sinon la
		// projection mélange l'échelle du HLZ et celle du panorama HD.
		recalculerGeometrie();
	}
	_dirty = true;
	_dirtyCoords = true;
}

void Omni3DManager::init(double hfov) {
	init(kDefaultRenderWidth, kDefaultRenderHeight, hfov);
}

void Omni3DManager::init(int width, int height, double hfov) {
	_hfov = hfov;
	_alpha = 0.;
	_beta = 0.;
	_xSpeed = 0.;
	_ySpeed = 0.;

	// La moitié droite des nœuds horizontaux est remplie par symétrie de
	// _oppositeV : la largeur doit donc être un multiple de 2 * kTileSize (32).
	// Une largeur multiple de 16 seulement (848) produit un artefact diagonal.
	if (width >= 2 * kTileSize && width <= kMaxCols * kTileSize &&
	        (width % (2 * kTileSize)) == 0) {
		_width = width;
		_cols = width / kTileSize;
	} else {
		_width = kDefaultRenderWidth;
		_cols = _width / kTileSize;
		warning("largeur de rendu %d refusée : multiple de %d attendu, au plus %d ; repli sur %d",
		        width, 2 * kTileSize, kMaxCols * kTileSize, _width);
	}
	if (height >= kTileSize && height <= kMaxRows * kTileSize &&
	        (height % kTileSize) == 0) {
		_height = height;
		_rows = height / kTileSize;
	} else {
		_height = kDefaultRenderHeight;
		_rows = _height / kTileSize;
		warning("hauteur de rendu %d refusée : multiple de %d attendu, au plus %d ; repli sur %d",
		        height, kTileSize, kMaxRows * kTileSize, _height);
	}

	recalculerGeometrie();

	_surface.create(_width, _height, Graphics::PixelFormat::createFormatCLUT8());
	clearConstraints();
}

Omni3DManager::~Omni3DManager() {
	_surface.free();
}

void Omni3DManager::updateCoords(int xDelta, int yDelta, bool useOldSpeed) {
	double xDelta1 = xDelta * 0.00025;
	double yDelta1 = yDelta * 0.0002;

	if (useOldSpeed) {
		_xSpeed += xDelta1;
		_ySpeed += yDelta1;
	} else {
		_xSpeed = xDelta1;
		_ySpeed = yDelta1;
	}
	_alpha += _xSpeed;
	_beta += _ySpeed;

	//debug("alpha = %lf beta = %lf xSpeed = %lf ySpeed = %lf", _alpha, _beta, _xSpeed, _ySpeed);

	_xSpeed *= 0.4;
	_ySpeed *= 0.6;

	if (useOldSpeed) {
		if (fabs(_xSpeed) < 0.001) {
			_xSpeed = 0.;
		}
		if (fabs(_ySpeed) < 0.001) {
			_ySpeed = 0.;
		}
	}

	if (_alpha < _alphaMin) {
		_alpha = _alphaMin;
		_xSpeed = 0.;
	} else if (_alpha > _alphaMax) {
		_alpha = _alphaMax;
		_xSpeed = 0.;
	}
	if (_beta < _betaMin) {
		_beta = _betaMin;
		_ySpeed = 0.;
	} else if (_beta > _betaMax) {
		_beta = _betaMax;
		_ySpeed = 0.;
	}

	if (_alpha >= 2. * M_PI) {
		_alpha -= 2. * M_PI;
	} else if (_alpha < 0.) {
		_alpha += 2. * M_PI;
	}

	_dirtyCoords = true;

	updateImageCoords();
}

void Omni3DManager::updateImageCoords() {
	if (!_dirtyCoords) {
		return;
	}

	if (_alpha >= 2.*M_PI) {
		_alpha -= 2.*M_PI;
	} else if (_alpha < 0) {
		_alpha += 2.*M_PI;
	}
	if (_beta > 0.9 * _vfov) {
		_beta = 0.9 * _vfov;
	} else if (_beta < -0.9 * _vfov) {
		_beta = -0.9 * _vfov;
	}

	// Le décalage du centre doit suivre l'échelle de la source, comme _helperValue :
	// avec un panorama ×4 (8192) et un 2048 en dur, le nœud central — écrit en absolu
	// plus bas — tombe un quart de tour à côté de ses voisins, et le carreau qui
	// l'enjambe balaie des milliers de pixels source en 16 px d'écran.
	double tmp = _srcWidth * 65536. - _helperValue * _alpha;

	uint k = 0;
	uint demiCols = (uint)(_cols / 2);
	for (uint i = 0; i < (uint)_rows + 1; i++) {
		double v11 = _anglesH[i] + _beta;
		double v26 = sin(v11);
		double v25 = cos(v11) * _hypothenusesH[i];

		uint offset = demiCols * 4;
		uint j;
		for (j = 0; j < demiCols; j++) {
			double v16 = atan2(_oppositeV[j], v25);
			double v17 = v16 * _helperValue;
			double v18 = ((_srcHeight / 2) * 65536) - _squaresCoords[i][j] * v26;

			k += 2;
			_imageCoords[k + 0] = (int)(tmp + v17);
			_imageCoords[k + offset + 0] = (int)(tmp - v17);
			_imageCoords[k + 1] = (int) v18;
			_imageCoords[k + offset + 1] = (int) v18;

			offset -= 4;
		}

		double v19 = atan2(_oppositeV[j], v25);

		k += 2;
		_imageCoords[k + 0] = (int)((_srcWidth * 65536.) - (_alpha - v19) * _helperValue);
		_imageCoords[k + 1] = (int)(((_srcHeight / 2) * 65536.) - _squaresCoords[i][j] * v26);

		k += demiCols * 2;
	}

	_dirtyCoords = false;
	_dirty = true;
}

const Graphics::Surface *Omni3DManager::getSurface() {
	const uint32 debutTravail = g_system->getMillis();
	if (!_sourceSurface) {
		return nullptr;
	}

#ifdef DUMP_OMNI3D
	static bool s_dumped = false;
	if (!s_dumped) {
		s_dumped = true;
		_alpha = 0.;
		_beta = 0.;
		_dirtyCoords = true;
		updateImageCoords();
	}
#endif

	if (_dirtyCoords) {
		updateImageCoords();
	}

	if (_dirty) {
		// Rendu du cylindre : pour chaque carreau de la grille, on interpole
		// bilinéairement les coordonnées source (16.16) de ses quatre coins.
		// L'original faisait le même travail en fixed-point, mais avec un packing
		// d'offsets calibré pour une source de 2048 de large, impossible à élargir.
		// Cette version est paramétrable par kSrcWidth / kSrcHeight.
		byte *dst = (byte *)_surface.getBasePtr(0, 0);
		const byte *src = (const byte *)_sourceSurface->getBasePtr(0, 0);
		const int pas = 2 * (_cols + 1);
		const int masqueX = _srcWidth - 1;   // _srcWidth est une puissance de deux

		for (int ty = 0; ty < _rows; ty++) {
			for (int tx = 0; tx < _cols; tx++) {
				// +2 : updateImageCoords commence a remplir a l'indice 2 ; [0] et [1] ne
				// sont jamais ecrits et contiennent des dechets.
				const int i00 = ty * pas + 2 * tx + 2;
				const int x00 = _imageCoords[i00],          y00 = _imageCoords[i00 + 1];
				const int x10 = _imageCoords[i00 + 2],      y10 = _imageCoords[i00 + 3];
				const int x01 = _imageCoords[i00 + pas],    y01 = _imageCoords[i00 + pas + 1];
				int x11 = _imageCoords[i00 + pas + 2];
				const int y11 = _imageCoords[i00 + pas + 3];

				// Raccord du cylindre : deux coins voisins peuvent tomber de part et
				// d'autre de la couture (x = 2047 et x = 0). Sans ce recalage, leur
				// interpolation traverse toute l'image et produit un déchirement.
				const int64 demi = (int64)_srcWidth << 15;   // moitié de la largeur, en 16.16
				const int64 tour = (int64)_srcWidth << 16;
				int x10r = x10, x01r = x01;
				if (x10r - x00 > demi) x10r -= tour; else if (x00 - x10r > demi) x10r += tour;
				if (x01r - x00 > demi) x01r -= tour; else if (x00 - x01r > demi) x01r += tour;
				if (x11 - x00 > demi) x11 -= tour; else if (x00 - x11 > demi) x11 += tour;

				for (int y = 0; y < kTileSize; y++) {
					for (int x = 0; x < kTileSize; x++) {
						const int64 wx0 = (int64)(kTileSize - x) * (kTileSize - y);
						const int64 wx1 = (int64)x * (kTileSize - y);
						const int64 wx2 = (int64)(kTileSize - x) * y;
						const int64 wx3 = (int64)x * y;
						const int64 sx = ((int64)x00 * wx0 + (int64)x10r * wx1 +
						                 (int64)x01r * wx2 + (int64)x11 * wx3) >> 8;
						const int64 sy = ((int64)y00 * wx0 + (int64)y10 * wx1 +
						                 (int64)y01 * wx2 + (int64)y11 * wx3) >> 8;
						int px = (int)(sx >> 16) & masqueX;
						int py = (int)(sy >> 16);
						if (py < 0) py = 0;
						else if (py >= _srcHeight) py = _srcHeight - 1;
						*dst++ = src[py * _srcWidth + px];
					}
					dst += _width - kTileSize;
				}
				dst -= (int64)_width * kTileSize - kTileSize;
			}
			dst += (int64)_width * (kTileSize - 1);
		}

		_dirty = false;
	}

#ifdef DUMP_OMNI3D
	{
		Common::DumpFile fp;
		if (fp.open("/tmp/omni3d-params.txt")) {
			for (int q = 0; q < 12; q++) {
				fp.writeString(Common::String::format("coords[%d]=%d\n", q, _imageCoords[q]));
			}
			fp.close();
		}
		// Tout le tableau de projection : permet de rejouer mapMouseCoords hors du
		// jeu (outils/verifier-jouabilite.py) et de comparer deux formules pour
		// TOUTES les positions de la souris, pas seulement celles qu'on clique.
		Common::DumpFile fc;
		if (fc.open("/tmp/omni3d-coords.bin")) {
			fc.writeUint32BE(_srcWidth);
			fc.writeUint32BE(_srcHeight);
			fc.writeUint32BE(_width);
			fc.writeUint32BE(_height);
			fc.writeUint32BE(_cols);
			fc.writeUint32BE(_rows);
			// +2 : la formule de mapMouseCoords lit les coins du dernier carreau
			// deux entiers au-delà de la dernière ligne écrite par updateImageCoords.
			const int total = 2 * (_cols + 1) * (_rows + 1) + 2;
			for (int q = 0; q < total; q++) {
				fc.writeSint32BE(_imageCoords[q]);
			}
			fc.close();
		}
		Common::DumpFile f;
		if (f.open("/tmp/omni3d-dump.pgm")) {
			f.writeString(Common::String::format("P5\n%d %d\n255\n", _surface.w, _surface.h));
			f.write(_surface.getBasePtr(0, 0), _surface.w * _surface.h);
			f.close();
		}
	}
#endif

	// Mesure de fluidité : combien d'images de panorama sont rendues par
	// seconde et combien de temps coûte chacune. À retirer une fois le
	// diagnostic fait (bloc isolé, rien d'autre ne dépend de lui).
	{
		static uint32 s_dernier = 0;
		static int s_images = 0;
		static uint32 s_travail = 0;
		const uint32 maintenant = g_system->getMillis();
		if (s_dernier == 0) {
			s_dernier = maintenant;
		}
		s_images++;
		s_travail += maintenant - debutTravail;
		if (maintenant - s_dernier >= 2000) {
			const uint32 ecoule = maintenant - s_dernier;
			warning("FPS %dx%d : %.1f images/s, %.1f ms de calcul par image",
			        _width, _height, s_images * 1000.0 / ecoule,
			        (double)s_travail / s_images);
			s_dernier = maintenant;
			s_images = 0;
			s_travail = 0;
		}
		(void)s_travail;
	}

	return &_surface;
}

void Omni3DManager::clearConstraints() {
	_alphaMin = -HUGE_VAL;
	_alphaMax = HUGE_VAL;
	_betaMin = -HUGE_VAL;
	_betaMax = HUGE_VAL;
}

Common::Point Omni3DManager::mapMouseCoords(const Common::Point &mouse) {
	Common::Point pt;

	if (_dirtyCoords) {
		updateImageCoords();
	}

	int smallX = mouse.x & (kTileSize - 1), squareX = mouse.x / kTileSize;
	int smallY = mouse.y & (kTileSize - 1), squareY = mouse.y / kTileSize;

	uint pas = 2 * (uint)(_cols + 1);
	uint off = pas * squareY + 2 * squareX;

	// Les quatre coins du carreau, en 16.16. On leur applique le MÊME recalage de
	// couture que le rendu (getSurface) : sans lui, un carreau qui enjambe le
	// raccord du cylindre interpole d'un bord à l'autre du panorama et le clic
	// tombe à l'opposé de ce que le joueur voit.
	int64 x00 = _imageCoords[off + 2];
	int64 x10 = _imageCoords[off + 4];
	int64 x01 = _imageCoords[off + pas + 2];
	int64 x11 = _imageCoords[off + pas + 4];
	const int64 demi = (int64)_srcWidth << 15;
	const int64 tour = (int64)_srcWidth << 16;
	if (x10 - x00 > demi) x10 -= tour; else if (x00 - x10 > demi) x10 += tour;
	if (x01 - x00 > demi) x01 -= tour; else if (x00 - x01 > demi) x01 += tour;
	if (x11 - x00 > demi) x11 -= tour; else if (x00 - x11 > demi) x11 += tour;

	const int64 vx = x00 +
	                smallY * ((x01 - x00) >> 4) +
	                (smallX * smallY) * ((x11 - x01) >> 8) +
	                (smallX * (kTileSize - smallY)) * ((x10 - x00) >> 8);
	const int64 vy = _imageCoords[off + 3] +
	                smallY * ((_imageCoords[off + pas + 3] - _imageCoords[off + 3]) >> 4) +
	                (smallX * smallY) * ((_imageCoords[off + pas + 5] - _imageCoords[off + pas + 3]) >> 8) +
	                (smallX * (kTileSize - smallY)) * ((_imageCoords[off + 5] - _imageCoords[off + 3]) >> 8);

	// Le panorama est replié sur lui-même (couture), puis ramené au repère que
	// tout le reste du moteur attend : celui du panorama d'origine, 2048×768.
	// Un panorama agrandi (×4 = 8192×3072) donnerait sinon des coordonnées
	// quatre fois trop grandes aux zones de clic.
	int px = (int)(vx >> 16) % _srcWidth;
	if (px < 0) px += _srcWidth;
	pt.x = px * kDefaultSrcWidth / _srcWidth;
	pt.y = (int)(vy >> 16) * kDefaultSrcHeight / _srcHeight;

#ifdef TRACE_JOUEABILITE
	// Trace de la conversion réelle, une ligne par déplacement de souris : prouve
	// que le jeu reçoit bien la coordonnée attendue par les zones de clic.
	static int s_dernierX = -1, s_dernierY = -1;
	if (mouse.x != s_dernierX || mouse.y != s_dernierY) {
		s_dernierX = mouse.x;
		s_dernierY = mouse.y;
		warning("MOUSE %d,%d -> %d,%d  coins %d %d | %d %d  alpha=%.5f beta=%.5f",
		        mouse.x, mouse.y, pt.x, pt.y,
		        _imageCoords[off + 2], _imageCoords[off + 4],
		        _imageCoords[off + pas + 2], _imageCoords[off + pas + 4],
		        _alpha, _beta);
	}
#endif

	return pt;
}

} // End of namespace CryOmni3D
