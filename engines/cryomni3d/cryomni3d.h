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

#ifndef CRYOMNI3D_CRYOMNI3D_H
#define CRYOMNI3D_CRYOMNI3D_H

#include "audio/mixer.h"

#include "common/array.h"
#include "common/keyboard.h"
#include "common/queue.h"
#include "common/rect.h"
#include "common/scummsys.h"

#include "engines/engine.h"

#include "graphics/cursorman.h"
#include "graphics/managed_surface.h"

#include "cryomni3d/font_manager.h"
#include "cryomni3d/objects.h"
#include "cryomni3d/sprites.h"
#include "cryomni3d/detection.h"

class OSystem;

namespace Common {
struct Point;
class SeekableReadStream;
}

namespace Image {
class ImageDecoder;
}

/**
 * This is the namespace of the Cryo Omni3D engine.
 *
 * Status of this engine: ???
 *
 * Games using this engine:
 * - Versailles
 * - ...
 */
namespace CryOmni3D {

class DATSeekableStream;

// Engine Debug Flags
enum {
	kDebugFile = 1,
	kDebugVariable,
	kDebugSaveLoad,
};

enum DragStatus {
	kDragStatus_NoDrag = 0,
	kDragStatus_Pressed,
	kDragStatus_Finished,
	kDragStatus_Dragging
};

class CryOmni3DEngine : public ::Engine {
protected:
	Common::Error run() override;

public:
	CryOmni3DEngine(OSystem *syst, const CryOmni3DGameDescription *gamedesc);
	~CryOmni3DEngine() override;

	// Detection related functions
	const CryOmni3DGameDescription *_gameDescription;
	const char *getGameId() const;
	uint32 getFeatures() const;
	uint16 getVersion() const;
	Common::Platform getPlatform() const;
	uint8 getGameType() const;
	Common::Language getLanguage() const;

	bool hasFeature(EngineFeature f) const override;
	bool canLoadGameStateCurrently(Common::U32String *msg = nullptr) override { return _canLoadSave; }
	bool canSaveGameStateCurrently(Common::U32String *msg = nullptr) override { return _canLoadSave; }

	void setCanLoadSave(bool canLoadSave) { _canLoadSave = canLoadSave; }
	static const uint kSaveDescriptionLen = 20;
private:
	void pauseEngineIntern(bool) override;

public:
	Image::ImageDecoder *loadHLZ(const Common::Path &filepath);

	void fillSurface(byte color);

	/**
	 * Affiche un rectangle de pixels indexés.
	 *
	 * Le moteur reste entièrement en CLUT8 en interne (transparence par index,
	 * fontes, fondus) : sur un écran indexé c'est un passe-plat, sur un écran
	 * en vraies couleurs la conversion se fait ici, au dernier moment, avec la
	 * palette effectivement appliquée.
	 */
	void copyToScreen(const void *buf, int pitch, int x, int y, int w, int h);

	// --- Interface 640x480 mise à l'échelle de la surface de rendu ---
	//
	// Tous les écrans d'interface du jeu (menus, images fixes, documentation,
	// dialogues, barre d'outils) sont dessinés en 640x480. Depuis que la surface
	// de rendu fait 1920x1088 (ou 2560x1088), ils doivent être agrandis et
	// positionnés : c'est ce que fait cette couche. Les surfaces restent en
	// 640x480, seule la présentation change.
	//
	// Échelle ENTIÈRE (x2 en 1920x1088 comme en 2560x1088), centrée : l'art est
	// du 4:3 et ne doit pas être étiré en 16:9 (textes et visages déformés), et
	// un facteur entier garde les pixels carrés et nets (plus proche voisin).
	// Les bandes laissées libres sont noires.
	static const int kUIWidth = 640;
	static const int kUIHeight = 480;
	static const int kUIToolbarHeight = 60;

	/** Recalcule échelle et centrage. À appeler juste après initGraphics(). */
	void initUILayout();
	int getUIScale() const { return _uiScale; }
	int getUIOffsetX() const { return _uiOffsetX; }
	int getUIOffsetY() const { return _uiOffsetY; }
	/** Ordonnée, dans le repère 640x480, du haut de la barre d'outils affichée. */
	int getUIToolbarTop() const;
	Common::Point mapScreenToUI(const Common::Point &p) const;
	/** Position de la souris ramenée dans le repère 640x480 de l'interface. */
	Common::Point getUIMousePos();
	void setUIMousePos(const Common::Point &p);

	/** Affiche une région d'une surface d'interface à sa place logique (mise à l'échelle). */
	void copyUIAt(const Graphics::Surface &src, const Common::Rect &srcRect);
	/** Affiche une surface d'interface plein écran : efface l'écran puis la centre. */
	void copyUIScreen(const Graphics::Surface &src);
	/** Affiche une région d'interface à une position écran imposée (barre, compte à rebours). */
	void copyUIAtScreen(const Graphics::Surface &src, const Common::Rect &srcRect, int screenX, int screenY);
	/** Affiche la barre d'outils, collée au bas de l'écran et centrée horizontalement. */
	void copyUIToolbar(const Graphics::Surface &src);
	/** Capture, en 640x60, ce qui est réellement affiché derrière la barre d'outils. */
	void captureToolbarBackground(const Graphics::Surface &displayed, Graphics::ManagedSurface &strip);
	void clearScreen();

	/* We use CursorMan because it avoids problems with cursors in GMM */
	void setCursor(const Graphics::Cursor &cursor) const;
	void setCursor(uint cursorId) const;
	bool showMouse(bool visible) { return CursorMan.showMouse(visible); }
	typedef void (CryOmni3DEngine::*HNMCallback)(uint frameNum);
	void playHNM(const Common::Path &filepath,
	             Audio::Mixer::SoundType soundType = Audio::Mixer::kPlainSoundType,
	             HNMCallback beforeDraw = nullptr, HNMCallback afterDraw = nullptr);
	bool displayHLZ(const Common::Path &filepath, uint32 timeout = uint(-1));

	bool pollEvents();
	Common::Point getMousePos();
	void setMousePos(const Common::Point &point);
	uint getCurrentMouseButton() { return _lastMouseButton; }
	Common::KeyState getNextKey();
	bool checkKeysPressed();
	bool checkKeysPressed(uint numKeys, ...);
	void clearKeys() { _keysPressed.clear(); }
	void waitMouseRelease();
	void setAutoRepeatClick(uint millis);
	DragStatus getDragStatus() { return _dragStatus; }

	virtual bool displayToolbar(const Graphics::Surface *original) = 0;
	virtual bool hasPlaceDocumentation() = 0;
	virtual bool displayPlaceDocumentation() = 0;
	virtual uint displayOptions() = 0;
	virtual bool shouldAbort() { return g_engine->shouldQuit(); }

	virtual void makeTranslucent(Graphics::Surface &dst, const Graphics::Surface &src) const = 0;
	virtual void setupPalette(const byte *colors, uint start, uint num) = 0;

protected:
	DATSeekableStream *getStaticData(uint32 gameId, uint16 version) const;

	void copySubPalette(byte *dst, const byte *src, uint start, uint num);
	void setPalette(const byte *colors, uint start, uint num);
	void lockPalette(uint startRW, uint endRW) { _lockPaletteStartRW = startRW; _lockPaletteEndRW = endRW; }
	void unlockPalette() { _lockPaletteStartRW = 0; _lockPaletteEndRW = 255; }
	void fadeOutPalette();
	void fadeInPalette(const byte *colors);
	void setBlackPalette();

	void setHNMClipping(const Common::Rect &clip) { _hnmClipping = clip; _hnmHasClip = true; }
	void unsetHNMClipping() { _hnmHasClip = false; }

protected:
	bool _canLoadSave;

	FontManager _fontManager;
	Sprites _sprites;
	Objects _objects;
	Inventory _inventory;

	Common::Queue<Common::KeyState> _keysPressed;

	DragStatus _dragStatus;
	Common::Point _dragStart;
	uint _lastMouseButton;
	uint _autoRepeatNextEvent;

private:
	uint _lockPaletteStartRW;
	uint _lockPaletteEndRW;

	// Miroir de la palette RÉELLEMENT appliquée à l'écran. Indispensable quand
	// l'écran est en vraies couleurs : g_system->getPaletteManager() n'y décrit
	// plus rien, et c'est cette copie qui sert à convertir les rectangles.
	byte _screenPalette[256 * 3];

	// Surface RGB de conversion, réutilisée d'une copie à l'autre.
	Graphics::Surface _convSurface;

	// Dernière image composée à l'écran, en vraies couleurs. Sert aux fondus :
	// en RGB, changer la palette ne modifie plus des pixels déjà écrits.
	Graphics::Surface _composed;
	Graphics::Surface _fadeBuffer;
	void drawComposedWithFade(int factor);
	void drawSurfaceWithFade(const Graphics::Surface &source, int factor);

	Common::Rect _hnmClipping;
	bool _hnmHasClip;

	// Mise à l'échelle de l'interface (cf. initUILayout).
	int _uiScale;
	int _uiOffsetX;
	int _uiOffsetY;
	Graphics::ManagedSurface _uiScaled;

	void scaleUIRegion(const Graphics::Surface &src, const Common::Rect &srcRect);
};

class CryOmni3DEngine_HNMPlayer : public CryOmni3DEngine {
protected:
	Common::Error run() override;

public:
	CryOmni3DEngine_HNMPlayer(OSystem *syst, const CryOmni3DGameDescription *gamedesc) : CryOmni3DEngine(syst, gamedesc) {}
	~CryOmni3DEngine_HNMPlayer() override {}

	bool displayToolbar(const Graphics::Surface *original) override { return false; }
	bool hasPlaceDocumentation() override { return false; }
	bool displayPlaceDocumentation() override { return false; }
	uint displayOptions() override { return 0; }
	void makeTranslucent(Graphics::Surface &dst, const Graphics::Surface &src) const override {}
	void setupPalette(const byte *colors, uint start, uint num) override {}
};

} // End of namespace CryOmni3D

#endif
