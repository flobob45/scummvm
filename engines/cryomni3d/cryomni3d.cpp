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

#include "common/scummsys.h"
#include "common/error.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/debug-channels.h"

#include "common/events.h"
#include "common/file.h"

#include "engines/util.h"

#include "audio/mixer.h"
#include "graphics/paletteman.h"

#include "cryomni3d/cryomni3d.h"
#include "cryomni3d/datstream.h"

#include "common/config-manager.h"

#include "image/png.h"

#include "cryomni3d/image/hlz.h"
#include "cryomni3d/image/hnm.h"
#include "video/hnm_decoder.h"

namespace CryOmni3D {

CryOmni3DEngine::CryOmni3DEngine(OSystem *syst,
								 const CryOmni3DGameDescription *gamedesc) : Engine(syst), _gameDescription(gamedesc),
	_canLoadSave(false), _fontManager(), _sprites(), _dragStatus(kDragStatus_NoDrag), _lastMouseButton(0),
	_autoRepeatNextEvent(uint(-1)), _hnmHasClip(false), _uiScale(1), _uiOffsetX(0), _uiOffsetY(0) {
	if (!_mixer->isReady()) {
		error("Sound initialization failed");
	}

	// Setup mixer
	syncSoundSettings();

	// Palette noire au départ : rien n'a encore été appliqué à l'écran.
	memset(_screenPalette, 0, sizeof(_screenPalette));

	unlockPalette();
}

CryOmni3DEngine::~CryOmni3DEngine() {
	_convSurface.free();
}

Common::Error CryOmni3DEngine::run() {
	return Common::kNoError;
}

void CryOmni3DEngine::pauseEngineIntern(bool pause) {
	Engine::pauseEngineIntern(pause);

	/*
	if (pause) {
	    _video->pauseVideos();
	} else {
	    _video->resumeVideos();
	    _system->updateScreen();
	}
	*/
}

DATSeekableStream *CryOmni3DEngine::getStaticData(uint32 gameId, uint16 version) const {
	Common::File *datFile = new Common::File();

	if (!datFile->open("cryomni3d.dat")) {
		delete datFile;
		error("Failed to open cryomni3d.dat file");
		return nullptr;
	}

	DATSeekableStream *gameStream = DATSeekableStream::getGame(datFile, gameId, version, getLanguage(),
	                                getPlatform());
	if (!gameStream) {
		delete datFile;
		error("Failed to find game in cryomni3d.dat file");
		return nullptr;
	}

	return gameStream;
}

void CryOmni3DEngine::playHNM(const Common::Path &filepath, Audio::Mixer::SoundType soundType,
							  HNMCallback beforeDraw, HNMCallback afterDraw) {
	// Les vidéos du jeu sont des HNM4/HNM5, que le décodeur ne sait produire
	// qu'en 8 bits : il refuse un format d'écran >= 2 octets. La conversion en
	// vraies couleurs se fait plus bas, dans copyToScreen, comme pour le reste.
	const Graphics::PixelFormat decoderFormat = Graphics::PixelFormat::createFormatCLUT8();

	// Le décodeur prend possession de la palette initiale : on lui donne une
	// copie de la palette réellement à l'écran (le miroir, car l'écran en vraies
	// couleurs ne permet pas de la relire).
	byte *currentPalette = new byte[256 * 3];
	memcpy(currentPalette, _screenPalette, 256 * 3);

	// Pass the ownership of currentPalette to HNMDecoder
	Video::VideoDecoder *videoDecoder = new Video::HNMDecoder(decoderFormat, false, currentPalette);
	videoDecoder->setSoundType(soundType);

	if (!videoDecoder->loadFile(filepath)) {
		warning("Failed to open movie file %s", filepath.toString(Common::Path::kNativeSeparator).c_str());
		delete videoDecoder;
		return;
	}

	videoDecoder->start();

	uint16 width = videoDecoder->getWidth();
	uint16 height = videoDecoder->getHeight();

	// La vidéo est un plein écran 4:3 : les bandes autour sont noires.
	clearScreen();

	bool skipVideo = false;
	uint frameNum = 0;
	while (!shouldAbort() && !videoDecoder->endOfVideo() && !skipVideo) {
		if (videoDecoder->needsUpdate()) {
			const Graphics::Surface *frame = videoDecoder->decodeNextFrame();

			if (frame) {
				if (videoDecoder->hasDirtyPalette()) {
					const byte *palette = videoDecoder->getPalette();
					setPalette(palette, 0, 256);
				}

				if (beforeDraw) {
					(this->*beforeDraw)(frameNum);
				}

				if (_hnmHasClip) {
					Common::Rect rct(width, height);
					rct.clip(_hnmClipping);
					copyUIAt(*frame, rct);
				} else {
					copyUIAt(*frame, Common::Rect(0, 0, width, height));
				}

				if (afterDraw) {
					(this->*afterDraw)(frameNum);
				}

				frameNum++;
			}
		}
		g_system->updateScreen();
		g_system->delayMillis(10);

		if (pollEvents() && checkKeysPressed()) {
			skipVideo = true;
		}
	}

	delete videoDecoder;
}

Image::ImageDecoder *CryOmni3DEngine::loadHLZ(const Common::Path &filepath) {
	warning("HLZCHARGE: %s", filepath.toString(Common::Path::kNativeSeparator).c_str());
	// Dossier de surcharge HD : à côté du HLZ, sous HD/, un PNG du même nom
	// (agrandi ×4 puis quantifié à 256 couleurs pour rester en CLUT8).
	// Son absence ramène au HLZ d'origine : la surcharge est optionnelle.
	Common::String base = filepath.baseName();
	const size_t point = base.findLastOf('.');
	if (point != Common::String::npos) {
		base = base.substr(0, point);
	}
	Common::Path hdPath = filepath.getParent()
	                      .appendComponent("HD")
	                      .appendComponent(base + ".png");
	// Common::File ne résout pas ce chemin : le fichier physique est sous DATAS_V/
	// avec des majuscules, alors que le chemin de jeu est relatif et en minuscules.
	// Pour le prototype on passe par le chemin absolu du jeu.
	// ScummVM sandboxe l'accès au dossier de jeu : on ouvre donc en relatif,
	// depuis la racine du jeu.
	Common::Path hdRel("hd_" + base + ".png");
	Common::File hdFile;
	if (hdFile.open(hdRel)) {
		Image::ImageDecoder *hdDecoder = new Image::PNGDecoder();
		if (hdDecoder->loadStream(hdFile)) {
			return hdDecoder;
		}
		warning("Failed to load HD image %s, falling back to HLZ",
		        hdPath.toString(Common::Path::kNativeSeparator).c_str());
		delete hdDecoder;
	}

	Common::File file;

	if (!file.open(filepath)) {
		warning("Failed to open hlz file %s", filepath.toString(Common::Path::kNativeSeparator).c_str());
		return nullptr;
	}

	Image::ImageDecoder *imageDecoder = new Image::HLZFileDecoder();

	if (!imageDecoder->loadStream(file)) {
		warning("Failed to load hlz file %s", filepath.toString(Common::Path::kNativeSeparator).c_str());
		delete imageDecoder;
		imageDecoder = nullptr;
		return nullptr;
	}

	return imageDecoder;
}

bool CryOmni3DEngine::displayHLZ(const Common::Path &filepath, uint32 timeout) {
	Image::ImageDecoder *imageDecoder = loadHLZ(filepath);

	if (!imageDecoder) {
		return false;
	}

	if (imageDecoder->hasPalette()) {
		setPalette(imageDecoder->getPalette().data(), 0, imageDecoder->getPalette().size());
	}

	const Graphics::Surface *frame = imageDecoder->getSurface();
	copyUIScreen(*frame);
	g_system->updateScreen();

	uint32 end;
	if (timeout == uint(-1)) {
		end = uint(-1);
	} else {
		end = g_system->getMillis() + timeout;
	}
	bool exitImg = false;
	while (!shouldAbort() && !exitImg && g_system->getMillis() < end) {
		if (pollEvents()) {
			if (checkKeysPressed() || getCurrentMouseButton() == 1) {
				exitImg = true;
			}
		}
		g_system->updateScreen();
		g_system->delayMillis(10);
	}

	delete imageDecoder;

	return exitImg || shouldAbort();
}

void CryOmni3DEngine::setCursor(const Graphics::Cursor &cursor) const {
	const Graphics::PixelFormat screenFormat = g_system->getScreenFormat();
	if (screenFormat.bytesPerPixel == 1) {
		CursorMan.replaceCursor(&cursor);
		return;
	}

	// Écran en vraies couleurs : le curseur reste indexé et sa transparence est
	// par INDEX (getKeyColor() renvoie 0). Le convertir en RGB à la main cassait
	// cette transparence et produisait un damier. On donne donc le curseur tel
	// quel au backend, avec la palette courante : c'est lui qui convertit.
	CursorMan.replaceCursor(&cursor);
	CursorMan.replaceCursorPalette(_screenPalette, 0, 256);
}

void CryOmni3DEngine::setCursor(uint cursorId) const {
	setCursor(_sprites.getCursor(cursorId));
}

bool CryOmni3DEngine::pollEvents() {
	Common::Event event;
	int buttonMask;
	bool hasEvents = false;

	// Don't take into transitional clicks for the drag
	buttonMask = g_system->getEventManager()->getButtonState();
	uint oldMouseButton;
	if (buttonMask & 0x1) {
		oldMouseButton = 1;
	} else if (buttonMask & 0x2) {
		oldMouseButton = 2;
	} else {
		oldMouseButton = 0;
	}

	int transitionalMask = 0;
	while (g_system->getEventManager()->pollEvent(event)) {
		if (event.type == Common::EVENT_KEYDOWN) {
			_keysPressed.push(event.kbd);
		} else if (event.type == Common::EVENT_LBUTTONDOWN) {
			transitionalMask |= Common::EventManager::LBUTTON;
		} else if (event.type == Common::EVENT_RBUTTONDOWN) {
			transitionalMask |= Common::EventManager::RBUTTON;
		}
		hasEvents = true;
	}

	// Merge current button state with any buttons pressed since last poll
	// That's to avoid missed clicks
	buttonMask = g_system->getEventManager()->getButtonState() |
	             transitionalMask;
	if (buttonMask & 0x1) {
		_lastMouseButton = 1;
	} else if (buttonMask & 0x2) {
		_lastMouseButton = 2;
	} else {
		_lastMouseButton = 0;
	}

	_dragStatus = kDragStatus_NoDrag;
	uint currentMouseButton = getCurrentMouseButton();
	if (!oldMouseButton && currentMouseButton == 1) {
		// Starting the drag
		_dragStatus = kDragStatus_Pressed;
		_dragStart = getMousePos();
	} else if (oldMouseButton == 1) {
		// We were already pressing
		if (currentMouseButton == 1) {
			// We are still pressing
			Common::Point delta = _dragStart - getMousePos();
			if (ABS(delta.x) > 2 || ABS(delta.y) > 2) {
				// We moved from the start point
				_dragStatus = kDragStatus_Dragging;
			} else if (_autoRepeatNextEvent != uint(-1)) {
				// Check for auto repeat duration
				if (_autoRepeatNextEvent < g_system->getMillis()) {
					_dragStatus = kDragStatus_Pressed;
				}
			}
		} else {
			// We just finished dragging
			_dragStatus = kDragStatus_Finished;
			// Cancel auto repeat
			_autoRepeatNextEvent = uint(-1);
		}
	}
	// Else we weren't dragging and still aren't

	return hasEvents;
}

void CryOmni3DEngine::setAutoRepeatClick(uint millis) {
	_autoRepeatNextEvent = g_system->getMillis() + millis;
}

void CryOmni3DEngine::waitMouseRelease() {
	while (getCurrentMouseButton() != 0 && !shouldAbort()) {
		pollEvents();
		g_system->updateScreen();
		g_system->delayMillis(10);
	}
}

void CryOmni3DEngine::setMousePos(const Common::Point &point) {
	g_system->warpMouse(point.x, point.y);
	// Ensure to update mouse position in event manager
	pollEvents();
}

Common::Point CryOmni3DEngine::getMousePos() {
	return g_system->getEventManager()->getMousePos();
}

Common::KeyState CryOmni3DEngine::getNextKey() {
	if (_keysPressed.empty()) {
		return Common::KeyState();
	} else {
		return _keysPressed.pop();
	}
}

bool CryOmni3DEngine::checkKeysPressed() {
	Common::KeyCode kc = getNextKey().keycode;
	if (kc != Common::KEYCODE_INVALID) {
		clearKeys();
		return true;
	} else {
		return false;
	}
}

bool CryOmni3DEngine::checkKeysPressed(uint numKeys, ...) {
	bool found = false;
	Common::KeyCode kc = getNextKey().keycode;
	while (!found && kc != Common::KEYCODE_INVALID) {
		va_list va;
		va_start(va, numKeys);
		for (uint i = 0; i < numKeys; i++) {
			// Compiler says that KeyCode is promoted to int, so we need this ugly cast
			Common::KeyCode match = (Common::KeyCode) va_arg(va, int);
			if (match == kc) {
				found = true;
				break;
			}
		}
		va_end(va);
		kc = getNextKey().keycode;
	}
	clearKeys();
	return found;
}

void CryOmni3DEngine::copySubPalette(byte *dst, const byte *src, uint start, uint num) {
	assert(start < 256);
	assert(start + num < 256);
	memcpy(&dst[3 * start], &src[3 * start], 3 * num * sizeof(*dst));
}

void CryOmni3DEngine::setPalette(const byte *colors, uint start, uint num) {
	if (start < _lockPaletteStartRW) {
		colors = colors + 3 * (_lockPaletteStartRW - start);
		start = _lockPaletteStartRW;
	}
	uint end = start + num - 1;
	if (end > _lockPaletteEndRW) {
		num = num - (end - _lockPaletteEndRW);
		end = _lockPaletteEndRW;
	}
	// Miroir de ce qui est réellement à l'écran : sur un écran en vraies
	// couleurs, getPaletteManager() ne décrit plus rien et c'est cette copie
	// qui sert à convertir les rectangles dans copyToScreen().
	memcpy(_screenPalette + 3 * start, colors, 3 * num);

	if (g_system->getScreenFormat().bytesPerPixel == 1) {
		g_system->getPaletteManager()->setPalette(colors, start, num);
	} else {
		// Le curseur est indexé : sa palette doit suivre celle de l'écran,
		// sinon il reste noir jusqu'au prochain changement de curseur.
		CursorMan.replaceCursorPalette(_screenPalette, 0, 256);
	}
	// Don't update screen there: palette will be updated with next updateScreen call
}

void CryOmni3DEngine::copyToScreen(const void *buf, int pitch, int x, int y, int w, int h) {
	const uint32 debutCopie = g_system->getMillis();
	// Diagnostic : une source indexee dont le pitch est plus court que la largeur
	// fait deriver chaque ligne -> triangle en damier vers le bas de l'ecran.
	if (pitch < w) {
		warning("COPIE INCOHERENTE : pitch=%d < largeur=%d (x=%d y=%d h=%d)", pitch, w, x, y, h);
	}
	const Graphics::PixelFormat screenFormat = g_system->getScreenFormat();
	if (screenFormat.bytesPerPixel == 1) {
		g_system->copyRectToScreen(buf, pitch, x, y, w, h);
		return;
	}

	// Surface de conversion réutilisée : en allouer une à chaque rectangle
	// faisait apparaître des images noires intermédiaires pendant les
	// transitions (l'écran est nettoyé puis redessiné en plusieurs morceaux).
	if (_convSurface.w != w || _convSurface.h != h || _convSurface.format != screenFormat) {
		_convSurface.create(w, h, screenFormat);
	}
	Graphics::Surface &conv = _convSurface;
	for (int row = 0; row < h; row++) {
		const byte *src = (const byte *)buf + row * pitch;
		byte *dst = (byte *)conv.getBasePtr(0, row);
		for (int col = 0; col < w; col++) {
			const byte *c = &_screenPalette[src[col] * 3];
			const uint32 color = screenFormat.RGBToColor(c[0], c[1], c[2]);
			if (screenFormat.bytesPerPixel == 2) {
				((uint16 *)dst)[col] = (uint16)color;
			} else {
				((uint32 *)dst)[col] = color;
			}
		}
	}
	// Mémoriser la composition : les fondus agissent désormais sur ces pixels,
	// changer la palette ne modifie plus une image en vraies couleurs.
	if (_composed.w != g_system->getWidth() || _composed.h != g_system->getHeight()) {
		_composed.create(g_system->getWidth(), g_system->getHeight(), screenFormat);
		memset(_composed.getPixels(), 0, _composed.pitch * _composed.h);
	}
	for (int row = 0; row < h; row++) {
		const int dy = y + row;
		if (dy < 0 || dy >= _composed.h || x >= _composed.w) {
			continue;
		}
		const int srcX = (x < 0) ? -x : 0;
		const int dstX = (x < 0) ? 0 : x;
		const int copie = MIN(w - srcX, _composed.w - dstX);
		if (copie > 0) {
			memcpy((byte *)_composed.getBasePtr(0, dy) + dstX * screenFormat.bytesPerPixel,
			       (const byte *)conv.getBasePtr(0, row) + srcX * screenFormat.bytesPerPixel,
			       copie * screenFormat.bytesPerPixel);
		}
	}

	g_system->copyRectToScreen(conv.getPixels(), conv.pitch, x, y, w, h);
	// Pas de conv.free() ici : sans lui, la surface est réellement réutilisée
	// d'un appel à l'autre, comme l'annonce le commentaire ci-dessus. La libérer
	// faisait échouer le test de taille et create() réallouait + remettait à zéro
	// plusieurs mégaoctets à chaque rectangle (à 2560x1072, 5,5 Mo par copie).
	// La copie vers le backend est synchrone (copyRectToScreen recopie aussitôt
	// dans la texture), donc conserver le tampon ne change rien à l'affichage.

	// Mesure de fluidité : coût de la conversion et de l'envoi à l'écran.
	// À retirer avec le compteur d'Omni3DManager une fois le diagnostic fait.
	{
		static uint32 s_dernier = 0;
		static int s_images = 0;
		static uint32 s_travail = 0;
		const uint32 maintenant = g_system->getMillis();
		if (s_dernier == 0) {
			s_dernier = maintenant;
		}
		s_images++;
		s_travail += maintenant - debutCopie;
		if (maintenant - s_dernier >= 2000) {
			const uint32 ecoule = maintenant - s_dernier;
			warning("COPIE %dx%d : %.1f copies/s, %.1f ms par copie",
			        w, h, s_images * 1000.0 / ecoule, (double)s_travail / s_images);
			s_dernier = maintenant;
			s_images = 0;
			s_travail = 0;
		}
	}
}

void CryOmni3DEngine::initUILayout() {
	const int screenW = g_system->getWidth();
	const int screenH = g_system->getHeight();

	// Facteur entier : x2 en 1920x1088 comme en 2560x1088. Si l'écran est plus
	// petit que 640x480 (repli indexé du backend), on garde x1.
	int scale = MIN((int)screenW / kUIWidth, (int)screenH / kUIHeight);
	if (scale < 1) {
		scale = 1;
	}

	_uiScale = scale;
	_uiOffsetX = (screenW - kUIWidth * scale) / 2;
	_uiOffsetY = (screenH - kUIHeight * scale) / 2;

	debug("CryOmni3D: interface %dx%d -> echelle %d, decalage %d,%d sur %dx%d",
	      kUIWidth, kUIHeight, scale, _uiOffsetX, _uiOffsetY, screenW, screenH);
}

int CryOmni3DEngine::getUIToolbarTop() const {
	// La barre est collée au bas de l'écran ; on exprime son haut dans le repère
	// 640x480 pour que les zones de clic restent inchangées.
	const int topScreen = g_system->getHeight() - kUIToolbarHeight * _uiScale;
	return (topScreen - _uiOffsetY) / _uiScale;
}

Common::Point CryOmni3DEngine::mapScreenToUI(const Common::Point &p) const {
	return Common::Point((p.x - _uiOffsetX) / _uiScale, (p.y - _uiOffsetY) / _uiScale);
}

Common::Point CryOmni3DEngine::getUIMousePos() {
	return mapScreenToUI(getMousePos());
}

void CryOmni3DEngine::setUIMousePos(const Common::Point &p) {
	setMousePos(Common::Point(_uiOffsetX + p.x * _uiScale, _uiOffsetY + p.y * _uiScale));
}

void CryOmni3DEngine::scaleUIRegion(const Graphics::Surface &src, const Common::Rect &srcRect) {
	const int dw = srcRect.width() * _uiScale;
	const int dh = srcRect.height() * _uiScale;

	if (_uiScaled.w != dw || _uiScaled.h != dh || _uiScaled.format.bytesPerPixel != 1) {
		_uiScaled.create(dw, dh, Graphics::PixelFormat::createFormatCLUT8());
	}

	// Plus proche voisin : l'art 640x480 est agrandi d'un facteur entier, chaque
	// pixel source devient un carré net de _uiScale x _uiScale pixels.
	for (int y = 0; y < dh; y++) {
		const byte *srow = (const byte *)src.getBasePtr(srcRect.left, srcRect.top + y / _uiScale);
		byte *drow = (byte *)_uiScaled.getBasePtr(0, y);
		for (int x = 0; x < dw; x++) {
			drow[x] = srow[x / _uiScale];
		}
	}
}

void CryOmni3DEngine::copyUIAt(const Graphics::Surface &src, const Common::Rect &srcRect) {
	scaleUIRegion(src, srcRect);
	copyToScreen(_uiScaled.getPixels(), _uiScaled.pitch,
	             _uiOffsetX + srcRect.left * _uiScale,
	             _uiOffsetY + srcRect.top * _uiScale,
	             _uiScaled.w, _uiScaled.h);
}

void CryOmni3DEngine::copyUIAtScreen(const Graphics::Surface &src, const Common::Rect &srcRect,
                                     int screenX, int screenY) {
	scaleUIRegion(src, srcRect);
	copyToScreen(_uiScaled.getPixels(), _uiScaled.pitch, screenX, screenY,
	             _uiScaled.w, _uiScaled.h);
}

void CryOmni3DEngine::copyUIScreen(const Graphics::Surface &src) {
	clearScreen();
	copyUIAt(src, Common::Rect(0, 0, src.w, src.h));
}

void CryOmni3DEngine::copyUIToolbar(const Graphics::Surface &src) {
	copyUIAtScreen(src, Common::Rect(0, 0, src.w, src.h),
	               _uiOffsetX, g_system->getHeight() - src.h * _uiScale);
}

void CryOmni3DEngine::captureToolbarBackground(const Graphics::Surface &displayed,
                                               Graphics::ManagedSurface &strip) {
	if (strip.w != kUIWidth || strip.h != kUIToolbarHeight || strip.format.bytesPerPixel != 1) {
		strip.create(kUIWidth, kUIToolbarHeight, Graphics::PixelFormat::createFormatCLUT8());
	}
	strip.fillRect(Common::Rect(0, 0, kUIWidth, kUIToolbarHeight), 0);

	const int topScreen = g_system->getHeight() - kUIToolbarHeight * _uiScale;

	if (displayed.w <= kUIWidth && displayed.h <= kUIHeight) {
		// Image d'interface 640x480 affichée centrée : la barre d'outils déborde
		// sous l'image, sur la bande noire. On copie la fin de l'image puis on
		// laisse le noir.
		const int uiTop = (topScreen - _uiOffsetY) / _uiScale;
		const int first = MAX(0, -uiTop);
		const int last = MIN(kUIToolbarHeight, displayed.h - uiTop);
		if (last > first) {
			Common::Rect srcRect(0, uiTop + first, MIN(kUIWidth, (int)displayed.w), uiTop + last);
			strip.copyRectToSurface(displayed, 0, first, srcRect);
		}
	} else {
		// Panorama : il occupe toute la surface de rendu, donc le bas de cette
		// surface EST ce qui est affiché derrière la barre.
		for (int y = 0; y < kUIToolbarHeight; y++) {
			const byte *srow = (const byte *)displayed.getBasePtr(_uiOffsetX, topScreen + y * _uiScale);
			byte *drow = (byte *)strip.getBasePtr(0, y);
			for (int x = 0; x < kUIWidth; x++) {
				drow[x] = srow[x * _uiScale];
			}
		}
	}
}

void CryOmni3DEngine::clearScreen() {
	const Graphics::PixelFormat screenFormat = g_system->getScreenFormat();
	if (screenFormat.bytesPerPixel == 1) {
		g_system->fillScreen(0);
	} else {
		g_system->fillScreen(screenFormat.RGBToColor(0, 0, 0));
	}
}

/**
 * Affiche la dernière image composée avec un facteur de luminosité.
 *
 * Sur un écran indexé, le fondu se faisait en modifiant la palette : l'image
 * affichée suivait. En vraies couleurs, les pixels sont figés dans le tampon
 * et changer la palette ne produit plus rien à l'écran — c'est pourquoi le
 * fondu était devenu invisible. On agit donc ici sur les pixels.
 *
 * @param factor 0 = noir, 256 = image normale
 */
void CryOmni3DEngine::drawComposedWithFade(int factor) {
	drawSurfaceWithFade(_composed, factor);
}

void CryOmni3DEngine::drawSurfaceWithFade(const Graphics::Surface &source, int factor) {
	if (source.w <= 0 || source.h <= 0) {
		return;
	}
	const Graphics::PixelFormat fmt = source.format;
	if (_fadeBuffer.w != source.w || _fadeBuffer.h != source.h) {
		_fadeBuffer.create(source.w, source.h, fmt);
	}
	for (int y = 0; y < source.h; y++) {
		const byte *src = (const byte *)source.getBasePtr(0, y);
		byte *dst = (byte *)_fadeBuffer.getBasePtr(0, y);
		for (int x = 0; x < source.w; x++) {
			uint32 color;
			if (fmt.bytesPerPixel == 2) {
				color = ((const uint16 *)src)[x];
			} else {
				color = ((const uint32 *)src)[x];
			}
			byte r, g, b;
			fmt.colorToRGB(color, r, g, b);
			r = (byte)(r * factor >> 8);
			g = (byte)(g * factor >> 8);
			b = (byte)(b * factor >> 8);
			color = fmt.RGBToColor(r, g, b);
			if (fmt.bytesPerPixel == 2) {
				((uint16 *)dst)[x] = (uint16)color;
			} else {
				((uint32 *)dst)[x] = color;
			}
		}
	}
	g_system->copyRectToScreen(_fadeBuffer.getPixels(), _fadeBuffer.pitch, 0, 0,
	                           _fadeBuffer.w, _fadeBuffer.h);
}

void CryOmni3DEngine::fadeOutPalette() {
	byte palOut[256 * 3];
	uint16 palWork[256 * 3];
	uint16 delta[256 * 3];

	// Sur un écran en vraies couleurs, getPaletteManager() n'a plus de palette
	// (le backend refuse même de la lire) : on repart de notre miroir.
	if (g_system->getScreenFormat().bytesPerPixel == 1) {
		g_system->getPaletteManager()->grabPalette(palOut, 0, 256);
	} else {
		memcpy(palOut, _screenPalette, sizeof(palOut));
	}
	for (uint i = 0; i < 256 * 3; i++) {
		palWork[i] = palOut[i] << 8;
		delta[i] = palWork[i] / 25;
	}

	for (uint step = 0; step < 25 && !shouldAbort(); step++) {
		for (uint i = 0; i < 256 * 3; i++) {
			palWork[i] -= delta[i];
			palOut[i] = palWork[i] >> 8;
		}
		setPalette(palOut, 0, 256);
		if (g_system->getScreenFormat().bytesPerPixel != 1) {
			drawComposedWithFade(256 - (int)((step + 1) * 256 / 25));
		}
		// Wait 50ms between each steps but refresh screen every 10ms
		for (uint i = 0; i < 5; i++) {
			pollEvents();
			g_system->updateScreen();
			g_system->delayMillis(10);
		}
	}
	setBlackPalette();
	pollEvents();
	g_system->updateScreen();
	clearKeys();
}

void CryOmni3DEngine::fadeInPalette(const byte *palette) {
	byte palOut[256 * 3];
	uint16 palWork[256 * 3];
	uint16 delta[256 * 3];

	memset(palOut, 0, sizeof(palOut));
	memset(palWork, 0, sizeof(palWork));
	for (uint i = 0; i < 256 * 3; i++) {
		delta[i] = (palette[i] << 8) / 25;
	}

	// Sauvegarder l'image AVANT de noircir : setBlackPalette écrase _composed.
	Graphics::Surface sauvegarde;
	const bool ecranRGB = g_system->getScreenFormat().bytesPerPixel != 1;
	if (ecranRGB && _composed.w > 0) {
		sauvegarde.copyFrom(_composed);
	}

	setBlackPalette();
	for (uint step = 0; step < 25 && !shouldAbort(); step++) {
		for (uint i = 0; i < 256 * 3; i++) {
			palWork[i] += delta[i];
			palOut[i] = palWork[i] >> 8;
		}
		setPalette(palOut, 0, 256);
		if (ecranRGB && sauvegarde.w > 0) {
			drawSurfaceWithFade(sauvegarde, (int)((step + 1) * 256 / 25));
		}
		// Wait 50ms between each steps but refresh screen every 10ms
		for (uint i = 0; i < 5; i++) {
			pollEvents();
			g_system->updateScreen();
			g_system->delayMillis(10);
		}
	}
	setPalette(palette, 0, 256);
	if (ecranRGB && sauvegarde.w > 0) {
		drawSurfaceWithFade(sauvegarde, 256);
	}
	pollEvents();
	g_system->updateScreen();
	clearKeys();
}

void CryOmni3DEngine::setBlackPalette() {
	byte pal[256 * 3];
	memset(pal, 0, 256 * 3);
	// Comme l'original, on noircit tout sans tenir compte des locks — mais on
	// tient le miroir à jour et on ne touche au gestionnaire que s'il en a un.
	memcpy(_screenPalette, pal, sizeof(pal));
	if (g_system->getScreenFormat().bytesPerPixel == 1) {
		g_system->getPaletteManager()->setPalette(pal, 0, 256);
	} else if (_composed.w > 0) {
		// En vraies couleurs, noircir la composition et l'afficher : sans cela,
		// l'écran garderait l'image précédente (le tampon est déjà en pixels).
		memset(_composed.getPixels(), 0, _composed.pitch * _composed.h);
		g_system->copyRectToScreen(_composed.getPixels(), _composed.pitch, 0, 0,
		                           _composed.w, _composed.h);
	}
	g_system->updateScreen();
}

void CryOmni3DEngine::fillSurface(byte color) {
	g_system->fillScreen(color);
	g_system->updateScreen();
}

Common::Error CryOmni3DEngine_HNMPlayer::run() {
	CryOmni3DEngine::run();

	initGraphics(640, 480);
	initUILayout();

	syncSoundSettings();

	for (int i = 0; _gameDescription->desc.filesDescriptions[i].fileName; i++) {
		playHNM(_gameDescription->desc.filesDescriptions[i].fileName, Audio::Mixer::kMusicSoundType);
	}

	return Common::kNoError;
}

} // End of namespace CryOmni3D
