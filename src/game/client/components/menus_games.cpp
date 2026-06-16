/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <base/math.h>
#include <base/system.h>

#include <engine/keys.h>
#include <engine/shared/localization.h>
#include <engine/textrender.h>

#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <deque>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

namespace FontIcons
{
// Extra icons used by the fun-games page (copied from BestClient icon table).
[[maybe_unused]] static const char *FONT_ICON_PLUS = "+";
[[maybe_unused]] static const char *FONT_ICON_MINUS = "-";
[[maybe_unused]] static const char *FONT_ICON_LOCK = "\xEF\x80\xA3";
[[maybe_unused]] static const char *FONT_ICON_MAGNIFYING_GLASS = "\xEF\x80\x82";
[[maybe_unused]] static const char *FONT_ICON_HEART = "\xEF\x80\x84";
[[maybe_unused]] static const char *FONT_ICON_HEART_CRACK = "\xEF\x9E\xA9";
[[maybe_unused]] static const char *FONT_ICON_STAR = "\xEF\x80\x85";
[[maybe_unused]] static const char *FONT_ICON_XMARK = "\xEF\x80\x8D";
[[maybe_unused]] static const char *FONT_ICON_CIRCLE = "\xEF\x84\x91";
[[maybe_unused]] static const char *FONT_ICON_ARROW_ROTATE_LEFT = "\xEF\x83\xA2";
[[maybe_unused]] static const char *FONT_ICON_ARROW_ROTATE_RIGHT = "\xEF\x80\x9E";
[[maybe_unused]] static const char *FONT_ICON_FLAG_CHECKERED = "\xEF\x84\x9E";
[[maybe_unused]] static const char *FONT_ICON_BAN = "\xEF\x81\x9E";
[[maybe_unused]] static const char *FONT_ICON_CIRCLE_CHEVRON_DOWN = "\xEF\x84\xBA";
[[maybe_unused]] static const char *FONT_ICON_KEY = "\xEF\x82\x84";
[[maybe_unused]] static const char *FONT_ICON_BOMB = "\xEF\x87\xA2";
[[maybe_unused]] static const char *FONT_ICON_GAMEPAD = "\xEF\x84\x9B";
[[maybe_unused]] static const char *FONT_ICON_CUBES = "\xEF\x86\xB3";
[[maybe_unused]] static const char *FONT_ICON_TABLE_TENNIS_PADDLE_BALL = "\xEF\x91\x9D";
[[maybe_unused]] static const char *FONT_ICON_GHOST = "\xEF\x9B\xA2";
[[maybe_unused]] static const char *FONT_ICON_DOVE = "\xEF\x92\xBA";
[[maybe_unused]] static const char *FONT_ICON_DRAGON = "\xEF\x9B\x95";
[[maybe_unused]] static const char *FONT_ICON_CAT = "\xEF\x9A\xBE";
[[maybe_unused]] static const char *FONT_ICON_HASHTAG = "\xEF\x8A\x92";
[[maybe_unused]] static const char *FONT_ICON_LIGHTBULB = "\xEF\x83\xAB";
[[maybe_unused]] static const char *FONT_ICON_CHESS_BISHOP = "\xEF\x90\xBA";
[[maybe_unused]] static const char *FONT_ICON_CHESS_KING = "\xEF\x90\xBF";
[[maybe_unused]] static const char *FONT_ICON_CHESS_KNIGHT = "\xEF\x91\x81";
[[maybe_unused]] static const char *FONT_ICON_CHESS_PAWN = "\xEF\x91\x83";
[[maybe_unused]] static const char *FONT_ICON_CHESS_QUEEN = "\xEF\x91\x85";
[[maybe_unused]] static const char *FONT_ICON_CHESS_ROOK = "\xEF\x91\x87";
[[maybe_unused]] static const char *FONT_ICON_SNAKE = "\xEF\xA0\xBE";
[[maybe_unused]] static const char *FONT_ICON_SQUARE_MINUS = "\xEF\x85\x86";
[[maybe_unused]] static const char *FONT_ICON_SQUARE_PLUS = "\xEF\x83\xBE";
[[maybe_unused]] static const char *FONT_ICON_SORT_UP = "\xEF\x83\x9E";
[[maybe_unused]] static const char *FONT_ICON_SORT_DOWN = "\xEF\x83\x9D";
[[maybe_unused]] static const char *FONT_ICON_TRIANGLE_EXCLAMATION = "\xEF\x81\xB1";
[[maybe_unused]] static const char *FONT_ICON_HOUSE = "\xEF\x80\x95";
[[maybe_unused]] static const char *FONT_ICON_BOOKMARK = "\xEF\x80\xAE";
[[maybe_unused]] static const char *FONT_ICON_NEWSPAPER = "\xEF\x87\xAA";
[[maybe_unused]] static const char *FONT_ICON_POWER_OFF = "\xEF\x80\x91";
[[maybe_unused]] static const char *FONT_ICON_GEAR = "\xEF\x80\x93";
[[maybe_unused]] static const char *FONT_ICON_PEN_TO_SQUARE = "\xEF\x81\x84";
[[maybe_unused]] static const char *FONT_ICON_CLAPPERBOARD = "\xEE\x84\xB1";
[[maybe_unused]] static const char *FONT_ICON_EARTH_AMERICAS = "\xEF\x95\xBD";
[[maybe_unused]] static const char *FONT_ICON_NETWORK_WIRED = "\xEF\x9B\xBF";
[[maybe_unused]] static const char *FONT_ICON_LIST_UL = "\xEF\x83\x8A";
[[maybe_unused]] static const char *FONT_ICON_INFO = "\xEF\x84\xA9";
[[maybe_unused]] static const char *FONT_ICON_TERMINAL = "\xEF\x84\xA0";
[[maybe_unused]] static const char *FONT_ICON_SLASH = "\xEF\x9C\x95";
[[maybe_unused]] static const char *FONT_ICON_PLAY = "\xEF\x81\x8B";
[[maybe_unused]] static const char *FONT_ICON_PAUSE = "\xEF\x81\x8C";
[[maybe_unused]] static const char *FONT_ICON_STOP = "\xEF\x81\x8D";
[[maybe_unused]] static const char *FONT_ICON_CHEVRON_LEFT = "\xEF\x81\x93";
[[maybe_unused]] static const char *FONT_ICON_CHEVRON_RIGHT = "\xEF\x81\x94";
[[maybe_unused]] static const char *FONT_ICON_CHEVRON_UP = "\xEF\x81\xB7";
[[maybe_unused]] static const char *FONT_ICON_CHEVRON_DOWN = "\xEF\x81\xB8";
[[maybe_unused]] static const char *FONT_ICON_BACKWARD = "\xEF\x81\x8A";
[[maybe_unused]] static const char *FONT_ICON_FORWARD = "\xEF\x81\x8E";
[[maybe_unused]] static const char *FONT_ICON_RIGHT_FROM_BRACKET = "\xEF\x8B\xB5";
[[maybe_unused]] static const char *FONT_ICON_RIGHT_TO_BRACKET = "\xEF\x8B\xB6";
[[maybe_unused]] static const char *FONT_ICON_ARROW_UP_RIGHT_FROM_SQUARE = "\xEF\x82\x8E";
[[maybe_unused]] static const char *FONT_ICON_BACKWARD_STEP = "\xEF\x81\x88";
[[maybe_unused]] static const char *FONT_ICON_FORWARD_STEP = "\xEF\x81\x91";
[[maybe_unused]] static const char *FONT_ICON_BACKWARD_FAST = "\xEF\x81\x89";
[[maybe_unused]] static const char *FONT_ICON_FORWARD_FAST = "\xEF\x81\x90";
[[maybe_unused]] static const char *FONT_ICON_KEYBOARD = "\xE2\x8C\xA8";
[[maybe_unused]] static const char *FONT_ICON_ELLIPSIS = "\xEF\x85\x81";
[[maybe_unused]] static const char *FONT_ICON_FOLDER = "\xEF\x81\xBB";
[[maybe_unused]] static const char *FONT_ICON_FOLDER_OPEN = "\xEF\x81\xBC";
[[maybe_unused]] static const char *FONT_ICON_FOLDER_TREE = "\xEF\xA0\x82";
[[maybe_unused]] static const char *FONT_ICON_FILM = "\xEF\x80\x88";
[[maybe_unused]] static const char *FONT_ICON_VIDEO = "\xEF\x80\xBD";
[[maybe_unused]] static const char *FONT_ICON_MAP = "\xEF\x89\xB9";
[[maybe_unused]] static const char *FONT_ICON_IMAGE = "\xEF\x80\xBE";
[[maybe_unused]] static const char *FONT_ICON_MUSIC = "\xEF\x80\x81";
[[maybe_unused]] static const char *FONT_ICON_FILE = "\xEF\x85\x9B";
[[maybe_unused]] static const char *FONT_ICON_PENCIL = "\xEF\x8C\x83";
[[maybe_unused]] static const char *FONT_ICON_TRASH = "\xEF\x87\xB8";
[[maybe_unused]] static const char *FONT_ICON_ARROWS_LEFT_RIGHT = "\xEF\x8C\xB7";
[[maybe_unused]] static const char *FONT_ICON_ARROWS_UP_DOWN = "\xEF\x81\xBD";
[[maybe_unused]] static const char *FONT_ICON_CIRCLE_PLAY = "\xEF\x85\x84";
[[maybe_unused]] static const char *FONT_ICON_BORDER_ALL = "\xEF\xA1\x8C";
[[maybe_unused]] static const char *FONT_ICON_EYE = "\xEF\x81\xAE";
[[maybe_unused]] static const char *FONT_ICON_EYE_SLASH = "\xEF\x81\xB0";
[[maybe_unused]] static const char *FONT_ICON_EYE_DROPPER = "\xEF\x87\xBB";
[[maybe_unused]] static const char *FONT_ICON_COMMENT = "\xEF\x81\xB5";
[[maybe_unused]] static const char *FONT_ICON_COMMENT_SLASH = "\xEF\x92\xB3";
[[maybe_unused]] static const char *FONT_ICON_DICE_ONE = "\xEF\x94\xA5";
[[maybe_unused]] static const char *FONT_ICON_DICE_TWO = "\xEF\x94\xA8";
[[maybe_unused]] static const char *FONT_ICON_DICE_THREE = "\xEF\x94\xA7";
[[maybe_unused]] static const char *FONT_ICON_DICE_FOUR = "\xEF\x94\xA4";
[[maybe_unused]] static const char *FONT_ICON_DICE_FIVE = "\xEF\x94\xA3";
[[maybe_unused]] static const char *FONT_ICON_DICE_SIX = "\xEF\x94\xA6";
[[maybe_unused]] static const char *FONT_ICON_LAYER_GROUP = "\xEF\x97\xBD";
[[maybe_unused]] static const char *FONT_ICON_UNDO = "\xEF\x8B\xAA";
[[maybe_unused]] static const char *FONT_ICON_REDO = "\xEF\x8B\xB9";
[[maybe_unused]] static const char *FONT_ICON_ARROWS_ROTATE = "\xEF\x80\xA1";
[[maybe_unused]] static const char *FONT_ICON_QUESTION = "?";
[[maybe_unused]] static const char *FONT_ICON_CAMERA = "\xEF\x80\xB0";
}

using namespace FontIcons;

namespace
{
	constexpr float FONT_SIZE = 14.0f;
	constexpr float LINE_SIZE = 20.0f;
	constexpr float HEADLINE_FONT_SIZE = 20.0f;
	constexpr float HEADLINE_HEIGHT = HEADLINE_FONT_SIZE;
	constexpr float MARGIN = 10.0f;
	constexpr float MARGIN_SMALL = 5.0f;
	constexpr float MARGIN_EXTRA_SMALL = 2.5f;
	constexpr float MARGIN_BETWEEN_VIEWS = 30.0f;

	enum EFunGame
	{
		FUN_GAME_SNAKE = 0,
		FUN_GAME_MINESWEEPER,
		FUN_GAME_CHESS,
		FUN_GAME_SUDOKU,
		FUN_GAME_MEMORY,
		FUN_GAME_DICE3D,
		FUN_GAME_PONG,
		FUN_GAME_DINOSAUR,
		FUN_GAME_BRICK_BREAKER,
		FUN_GAME_FOUR_IN_A_ROW,
		FUN_GAME_MINI_GOLF,
		FUN_GAME_CHECKERS,
		FUN_GAME_BATTLESHIP,
		FUN_GAME_FLOWMANIA,
		FUN_GAME_UNO,
		FUN_GAME_BILLIARDS,
		FUN_GAME_CASINO,
		NUM_FUN_GAMES
	};


	struct SFunGameInfo
	{
		const char *m_pName;
		const char *m_pIcon;
		const char *m_pHint;
	};

	const char *GetChessPieceIcon(char Piece)
	{
		switch((char)toupper((unsigned char)Piece))
		{
		case 'P': return FONT_ICON_CHESS_PAWN;
		case 'N': return FONT_ICON_CHESS_KNIGHT;
		case 'B': return FONT_ICON_CHESS_BISHOP;
		case 'R': return FONT_ICON_CHESS_ROOK;
		case 'Q': return FONT_ICON_CHESS_QUEEN;
		case 'K': return FONT_ICON_CHESS_KING;
		default: return nullptr;
		}
	}

	template<typename T>
	void ShuffleVector(std::vector<T> &vValues)
	{
		if(vValues.empty())
			return;
		for(int i = (int)vValues.size() - 1; i > 0; --i)
		{
			const int j = rand() % (i + 1);
			std::swap(vValues[i], vValues[j]);
		}
	}

	template<typename T>
	void UpdateSettingByClick(int Click, T &Value, T Min, T Max)
	{
		if(Click == 1)
			Value = Value >= Max ? Min : Value + 1;
		else if(Click == 2)
			Value = Value <= Min ? Max : Value - 1;
	}

	ColorRGBA BlendColors(const ColorRGBA &Base, const ColorRGBA &Overlay, float Factor)
	{
		const float T = std::clamp(Factor, 0.0f, 1.0f);
		return ColorRGBA(
			Base.r + (Overlay.r - Base.r) * T,
			Base.g + (Overlay.g - Base.g) * T,
			Base.b + (Overlay.b - Base.b) * T,
			Base.a + (Overlay.a - Base.a) * T);
	}
}

void CMenus::RenderSettingsBestClientFun(CUIRect MainView)
{
	static const SFunGameInfo s_aGames[NUM_FUN_GAMES] = {
		{"Snake", FONT_ICON_SNAKE, "Arrows/WASD, Space restart"},
		{"Minesweeper", FONT_ICON_BOMB, "LMB open, RMB flag, hover hints"},
		{"Chess", FONT_ICON_CHESS_KING, "Font Awesome chess pieces"},
		{"Sudoku", FONT_ICON_BORDER_ALL, "Click cell, type digits 1-9"},
		{"Memory", FONT_ICON_LAYER_GROUP, "Find all matching pairs"},
		{"3D Dice", FONT_ICON_DICE_SIX, "Roll animated 3D dice"},
		{"Pong", FONT_ICON_TABLE_TENNIS_PADDLE_BALL, "W/S or Up/Down to move paddle"},
		{"Dinosaur", FONT_ICON_DRAGON, "Space/W/Up to jump over cacti"},
		{"Brick Breaker", FONT_ICON_BORDER_ALL, "Break all bricks with the ball"},
		{"4 in a Row", FONT_ICON_CIRCLE, "Connect four before your opponent"},
		{"Mini Golf", FONT_ICON_FLAG_CHECKERED, "Simplified golf with levels"},
		{"Checkers", FONT_ICON_CHESS_PAWN, "Classic checkers board game"},
		{"Battleship", FONT_ICON_MAP, "Find and sink enemy ships"},
		{"Flowmania", FONT_ICON_ARROWS_ROTATE, "Connect color pairs without crossing"},
		{"UNO", FONT_ICON_LAYER_GROUP, "Match color or number cards"},
		{"Billiards", FONT_ICON_TABLE_TENNIS_PADDLE_BALL, "Pocket all balls with precision"},
		{"Casino", FONT_ICON_DICE_SIX, "Spin the reels, bet & win"}};

	static constexpr std::array<EFunGame, 7> s_aVisibleGames = {
		FUN_GAME_CASINO,
		FUN_GAME_SNAKE,
		FUN_GAME_MINESWEEPER,
		FUN_GAME_CHESS,
		FUN_GAME_MEMORY,
		FUN_GAME_PONG,
		FUN_GAME_BRICK_BREAKER,
	};

	auto IsVisibleGame = [&](int Game) {
		return std::find(s_aVisibleGames.begin(), s_aVisibleGames.end(), (EFunGame)Game) != s_aVisibleGames.end();
	};

	auto RenderIconLabel = [&](const CUIRect &Rect, const char *pIcon, float Size, int Align, const ColorRGBA *pColor = nullptr) {
		const ColorRGBA Color = pColor ? *pColor : TextRender()->DefaultTextColor();
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
		TextRender()->TextColor(Color);
		Ui()->DoLabel(&Rect, pIcon, Size, Align);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	};

	static int s_SelectedGame = FUN_GAME_CASINO;
	static CButtonContainer s_aGameButtons[NUM_FUN_GAMES];
	const float AnimTime = time_get() / (float)time_freq();

	if(!IsVisibleGame(s_SelectedGame))
		s_SelectedGame = (int)s_aVisibleGames[0];

	// Horizontal top bar with square game selector buttons (icon above, name below)
	const int NumVisible = (int)s_aVisibleGames.size();
	const float BtnGap = MARGIN_SMALL;
	const float BtnW = (MainView.w - BtnGap * (NumVisible - 1)) / (float)NumVisible;
	const float BtnH = minimum(BtnW, 95.0f);
	const float TopBarH = BtnH + MARGIN_SMALL;

	CUIRect TopBar, GameArea;
	MainView.HSplitTop(TopBarH, &TopBar, &GameArea);
	GameArea.HSplitTop(MARGIN_SMALL, nullptr, &GameArea);

	const ColorRGBA AreaColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.24f);
	GameArea.Draw(AreaColor, IGraphics::CORNER_ALL, 8.0f);

	{
		float BtnX = TopBar.x;
		const float BtnY = TopBar.y;

		for(EFunGame VisibleGame : s_aVisibleGames)
		{
			const int i = (int)VisibleGame;
			const bool Active = s_SelectedGame == i;
			CUIRect Button;
			Button.x = BtnX;
			Button.y = BtnY;
			Button.w = BtnW;
			Button.h = BtnH;
			const bool Hovered = Ui()->MouseInside(&Button);
			const ColorRGBA BtnBg = Active ? ColorRGBA(0.25f, 0.25f, 0.28f, 0.90f) : ColorRGBA(0.10f, 0.10f, 0.12f, 0.70f);
			Button.Draw(BtnBg, IGraphics::CORNER_ALL, 7.0f);
			if(Hovered && !Active)
				Button.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.06f), IGraphics::CORNER_ALL, 7.0f);

			if(DoButton_Menu(&s_aGameButtons[i], "", 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 7.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f)))
				s_SelectedGame = i;

			CUIRect IconArea;
			IconArea.x = BtnX;
			IconArea.y = BtnY + (Active ? sinf(AnimTime * 5.2f + i) * 1.2f : 0.0f);
			IconArea.w = BtnW;
			IconArea.h = BtnH * 0.60f;
			const ColorRGBA IconColor = Active ? ColorRGBA(0.95f, 0.95f, 0.95f, 0.95f) : Hovered ? ColorRGBA(0.92f, 0.92f, 0.92f, 0.85f) : ColorRGBA(0.80f, 0.80f, 0.85f, 0.72f);
			RenderIconLabel(IconArea, s_aGames[i].m_pIcon, IconArea.h * 0.72f, TEXTALIGN_MC, &IconColor);

			CUIRect NameArea;
			NameArea.x = BtnX;
			NameArea.y = BtnY + BtnH * 0.62f;
			NameArea.w = BtnW;
			NameArea.h = BtnH * 0.38f;
			const ColorRGBA NameColor = Active ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.95f) : ColorRGBA(0.85f, 0.85f, 0.88f, 0.72f);
			TextRender()->TextColor(NameColor);
			Ui()->DoLabel(&NameArea, TCLocalize(s_aGames[i].m_pName), FONT_SIZE * 0.78f, TEXTALIGN_MC);
			TextRender()->TextColor(TextRender()->DefaultTextColor());

			BtnX += BtnW + BtnGap;
		}
	}

	CUIRect GameContent;
	GameArea.Margin(MARGIN_SMALL, &GameContent);
	CUIRect GameHint;
	GameContent.HSplitTop(LINE_SIZE, &GameHint, &GameContent);
	GameContent.HSplitTop(MARGIN_SMALL, nullptr, &GameContent);

	Ui()->DoLabel(&GameHint, TCLocalize(s_aGames[s_SelectedGame].m_pHint), FONT_SIZE, TEXTALIGN_ML);

	if(s_SelectedGame == FUN_GAME_SNAKE)
	{
		struct SSnakeState
		{
			int m_SizePreset = 1;
			int m_SpeedPreset = 1;
			int m_Wrap = 0;

			bool m_Initialized = false;
			bool m_Waiting = true;
			int m_BoardW = 20;
			int m_BoardH = 14;
			std::deque<ivec2> m_Body;
			ivec2 m_Dir = ivec2(1, 0);
			ivec2 m_QueuedDir = ivec2(1, 0);
			bool m_HasQueuedDir = false;
			ivec2 m_Food = ivec2(-1, -1);
			int m_Grow = 0;
			int m_Score = 0;
			int m_BestScore = 0;
			bool m_GameOver = false;
			float m_TickAccumulator = 0.0f;
			int64_t m_LastTick = 0;
		};

		static SSnakeState s_Snake;

		auto ApplySnakePreset = [&]() {
			if(s_Snake.m_SizePreset == 0)
			{
				s_Snake.m_BoardW = 16;
				s_Snake.m_BoardH = 11;
			}
			else if(s_Snake.m_SizePreset == 1)
			{
				s_Snake.m_BoardW = 22;
				s_Snake.m_BoardH = 15;
			}
			else
			{
				s_Snake.m_BoardW = 28;
				s_Snake.m_BoardH = 19;
			}
		};

		auto PlaceFood = [&]() {
			const int MaxCells = s_Snake.m_BoardW * s_Snake.m_BoardH;
			if((int)s_Snake.m_Body.size() >= MaxCells)
			{
				s_Snake.m_Food = ivec2(-1, -1);
				return;
			}
			for(int Try = 0; Try < MaxCells * 2; ++Try)
			{
				const ivec2 Candidate(rand() % s_Snake.m_BoardW, rand() % s_Snake.m_BoardH);
				bool Occupied = false;
				for(const ivec2 &Part : s_Snake.m_Body)
				{
					if(Part == Candidate)
					{
						Occupied = true;
						break;
					}
				}
				if(!Occupied)
				{
					s_Snake.m_Food = Candidate;
					return;
				}
			}
		};

		auto ResetSnake = [&](bool KeepBestScore) {
			ApplySnakePreset();
			s_Snake.m_Initialized = true;
			s_Snake.m_Waiting = true;
			s_Snake.m_Body.clear();
			s_Snake.m_Dir = ivec2(1, 0);
			s_Snake.m_HasQueuedDir = false;
			s_Snake.m_Grow = 0;
			s_Snake.m_GameOver = false;
			s_Snake.m_TickAccumulator = 0.0f;
			s_Snake.m_LastTick = time_get();
			if(!KeepBestScore)
				s_Snake.m_BestScore = 0;
			s_Snake.m_Score = 0;
			const ivec2 Start(s_Snake.m_BoardW / 2, s_Snake.m_BoardH / 2);
			for(int i = 0; i < 3; ++i)
				s_Snake.m_Body.push_back(ivec2(Start.x - i, Start.y));
			PlaceFood();
		};

		auto QueueSnakeDir = [&](ivec2 Candidate) {
			if(s_Snake.m_HasQueuedDir)
				return;
			if(Candidate == s_Snake.m_Dir)
				return;
			if(Candidate == ivec2(-s_Snake.m_Dir.x, -s_Snake.m_Dir.y))
				return;
			s_Snake.m_QueuedDir = Candidate;
			s_Snake.m_HasQueuedDir = true;
		};

			if(!s_Snake.m_Initialized)
				ResetSnake(true);

			CUIRect TopBar, BoardArea;
			GameContent.HSplitTop(LINE_SIZE * 1.2f, &TopBar, &BoardArea);
			BoardArea.HSplitTop(MARGIN_SMALL, nullptr, &BoardArea);

			CUIRect ScoreLabel, BtnArea, RestartButton;
			TopBar.VSplitLeft(250.0f, &ScoreLabel, &BtnArea);
			BtnArea.VSplitRight(110.0f, &BtnArea, &RestartButton);

			char aScore[128];
			str_format(aScore, sizeof(aScore), "Score: %d   Best: %d", s_Snake.m_Score, s_Snake.m_BestScore);
			Ui()->DoLabel(&ScoreLabel, aScore, FONT_SIZE, TEXTALIGN_ML);
			static CButtonContainer s_SnakeRestartButton;
			if(DoButton_Menu(&s_SnakeRestartButton, TCLocalize("Restart"), 0, &RestartButton))
				ResetSnake(true);

			const float CellSize = minimum(BoardArea.w / s_Snake.m_BoardW, BoardArea.h / s_Snake.m_BoardH);
			CUIRect Board;
			Board.w = CellSize * s_Snake.m_BoardW;
			Board.h = CellSize * s_Snake.m_BoardH;
			Board.x = BoardArea.x + (BoardArea.w - Board.w) / 2.0f;
			Board.y = BoardArea.y + (BoardArea.h - Board.h) / 2.0f;

			const bool AnyKey = Input()->KeyPress(KEY_UP) || Input()->KeyPress(KEY_W) ||
				Input()->KeyPress(KEY_DOWN) || Input()->KeyPress(KEY_S) ||
				Input()->KeyPress(KEY_LEFT) || Input()->KeyPress(KEY_A) ||
				Input()->KeyPress(KEY_RIGHT) || Input()->KeyPress(KEY_D) ||
				Input()->KeyPress(KEY_SPACE) || Input()->KeyPress(KEY_RETURN) || Input()->KeyPress(KEY_KP_ENTER) ||
				(Ui()->MouseButtonClicked(0) && Ui()->MouseInside(&Board));

			if(s_Snake.m_Waiting)
			{
				if(AnyKey)
				{
					s_Snake.m_Waiting = false;
					s_Snake.m_LastTick = time_get();
				}
			}

			if(!s_Snake.m_Waiting)
			{
				if(Input()->KeyPress(KEY_UP) || Input()->KeyPress(KEY_W))
					QueueSnakeDir(ivec2(0, -1));
				if(Input()->KeyPress(KEY_DOWN) || Input()->KeyPress(KEY_S))
					QueueSnakeDir(ivec2(0, 1));
				if(Input()->KeyPress(KEY_LEFT) || Input()->KeyPress(KEY_A))
					QueueSnakeDir(ivec2(-1, 0));
				if(Input()->KeyPress(KEY_RIGHT) || Input()->KeyPress(KEY_D))
					QueueSnakeDir(ivec2(1, 0));
			}

			const bool PressRestart = Input()->KeyPress(KEY_SPACE) || Input()->KeyPress(KEY_RETURN) || Input()->KeyPress(KEY_KP_ENTER);
			const int64_t Now = time_get();
			float Dt = s_Snake.m_Waiting ? 0.0f : (Now - s_Snake.m_LastTick) / (float)time_freq();
			s_Snake.m_LastTick = Now;
			Dt = std::clamp(Dt, 0.0f, 0.05f);
			s_Snake.m_TickAccumulator += Dt;

			auto StepSnake = [&]() {
				if(s_Snake.m_HasQueuedDir)
				{
					s_Snake.m_Dir = s_Snake.m_QueuedDir;
					s_Snake.m_HasQueuedDir = false;
				}

				ivec2 Next = s_Snake.m_Body.front() + s_Snake.m_Dir;
				if(s_Snake.m_Wrap)
				{
					if(Next.x < 0)
						Next.x = s_Snake.m_BoardW - 1;
					if(Next.y < 0)
						Next.y = s_Snake.m_BoardH - 1;
					if(Next.x >= s_Snake.m_BoardW)
						Next.x = 0;
					if(Next.y >= s_Snake.m_BoardH)
						Next.y = 0;
				}
				else if(Next.x < 0 || Next.y < 0 || Next.x >= s_Snake.m_BoardW || Next.y >= s_Snake.m_BoardH)
				{
					s_Snake.m_GameOver = true;
					return;
				}

				for(const ivec2 &Part : s_Snake.m_Body)
				{
					if(Part == Next)
					{
						s_Snake.m_GameOver = true;
						return;
					}
				}

				s_Snake.m_Body.push_front(Next);
				if(Next == s_Snake.m_Food)
				{
					s_Snake.m_Score++;
					s_Snake.m_BestScore = maximum(s_Snake.m_BestScore, s_Snake.m_Score);
					s_Snake.m_Grow++;
					PlaceFood();
				}

				if(s_Snake.m_Grow > 0)
					s_Snake.m_Grow--;
				else if(!s_Snake.m_Body.empty())
					s_Snake.m_Body.pop_back();
			};

			if(!s_Snake.m_GameOver)
			{
				const float BaseSpeed = s_Snake.m_SpeedPreset == 0 ? 5.0f : s_Snake.m_SpeedPreset == 1 ? 7.0f :
															 9.0f;
				const float Speed = std::clamp(BaseSpeed + s_Snake.m_Score * 0.12f, BaseSpeed, 15.0f);
				const float Step = 1.0f / Speed;
				while(s_Snake.m_TickAccumulator >= Step)
				{
					s_Snake.m_TickAccumulator -= Step;
					StepSnake();
					if(s_Snake.m_GameOver)
					{
						s_Snake.m_TickAccumulator = 0.0f;
						break;
					}
				}
			}
			else if(PressRestart)
			{
				ResetSnake(true);
			}

			Board.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 4.0f);
			Board.DrawOutline(ColorRGBA(1.0f, 1.0f, 1.0f, 0.15f));

			if(s_Snake.m_Food.x >= 0 && s_Snake.m_Food.y >= 0)
			{
				CUIRect FoodRect;
				FoodRect.x = Board.x + s_Snake.m_Food.x * CellSize;
				FoodRect.y = Board.y + s_Snake.m_Food.y * CellSize;
				FoodRect.w = CellSize;
				FoodRect.h = CellSize;
				const float Pad = maximum(1.0f, CellSize * 0.14f);
				FoodRect.Margin(Pad, &FoodRect);
				FoodRect.Draw(ColorRGBA(1.0f, 0.55f, 0.2f, 0.95f), IGraphics::CORNER_ALL, 3.0f);
			}

			bool IsHead = true;
			for(const ivec2 &Part : s_Snake.m_Body)
			{
				CUIRect PartRect;
				PartRect.x = Board.x + Part.x * CellSize;
				PartRect.y = Board.y + Part.y * CellSize;
				PartRect.w = CellSize;
				PartRect.h = CellSize;
				const float Pad = maximum(1.0f, CellSize * 0.12f);
				PartRect.Margin(Pad, &PartRect);
				PartRect.Draw(IsHead ? ColorRGBA(0.3f, 0.85f, 1.0f, 1.0f) : ColorRGBA(0.1f, 0.64f, 0.9f, 0.95f), IGraphics::CORNER_ALL, 3.0f);
				IsHead = false;
			}

			if(s_Snake.m_Waiting)
			{
				CUIRect Overlay = Board;
				Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_ALL, 4.0f);
				Ui()->DoLabel(&Overlay, TCLocalize("Press any key to start"), HEADLINE_FONT_SIZE, TEXTALIGN_MC);
			}
			else if(s_Snake.m_GameOver)
			{
				CUIRect Overlay = Board;
				Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.45f), IGraphics::CORNER_ALL, 4.0f);
				Ui()->DoLabel(&Overlay, TCLocalize("Game Over"), HEADLINE_FONT_SIZE, TEXTALIGN_MC);
			}
	}
	else if(s_SelectedGame == FUN_GAME_MINESWEEPER)
	{
		struct SMinesweeperState
		{
			int m_Difficulty = 1;

			bool m_Initialized = false;
			int m_W = 12;
			int m_H = 10;
			int m_Bombs = 18;
			bool m_FirstOpen = true;
			bool m_GameOver = false;
			bool m_Won = false;
			int m_RevealedCount = 0;
			int m_Flags = 0;
			std::vector<int> m_vBoard;
			std::vector<uint8_t> m_vRevealed;
			std::vector<uint8_t> m_vFlagged;
		};

		static SMinesweeperState s_Mines;

		auto SetMinesDifficulty = [&]() {
			if(s_Mines.m_Difficulty == 0)
			{
				s_Mines.m_W = 9;
				s_Mines.m_H = 9;
				s_Mines.m_Bombs = 10;
			}
			else if(s_Mines.m_Difficulty == 1)
			{
				s_Mines.m_W = 12;
				s_Mines.m_H = 10;
				s_Mines.m_Bombs = 18;
			}
			else
			{
				s_Mines.m_W = 16;
				s_Mines.m_H = 12;
				s_Mines.m_Bombs = 32;
			}
		};

		auto MinesIndex = [&]() {
			return [&](int X, int Y) {
				return Y * s_Mines.m_W + X;
			};
		};

		auto ResetMines = [&]() {
			SetMinesDifficulty();
			s_Mines.m_Initialized = true;
			s_Mines.m_FirstOpen = true;
			s_Mines.m_GameOver = false;
			s_Mines.m_Won = false;
			s_Mines.m_RevealedCount = 0;
			s_Mines.m_Flags = 0;
			const int Cells = s_Mines.m_W * s_Mines.m_H;
			s_Mines.m_vBoard.assign(Cells, 0);
			s_Mines.m_vRevealed.assign(Cells, 0);
			s_Mines.m_vFlagged.assign(Cells, 0);
		};

		auto GenerateMines = [&](int SafeX, int SafeY) {
			std::fill(s_Mines.m_vBoard.begin(), s_Mines.m_vBoard.end(), 0);
			const auto Idx = MinesIndex();
			int Placed = 0;
			while(Placed < s_Mines.m_Bombs)
			{
				const int X = rand() % s_Mines.m_W;
				const int Y = rand() % s_Mines.m_H;
				if(abs(X - SafeX) <= 1 && abs(Y - SafeY) <= 1)
					continue;
				if(s_Mines.m_vBoard[Idx(X, Y)] == -1)
					continue;
				s_Mines.m_vBoard[Idx(X, Y)] = -1;
				Placed++;
			}

			for(int y = 0; y < s_Mines.m_H; ++y)
			{
				for(int x = 0; x < s_Mines.m_W; ++x)
				{
					if(s_Mines.m_vBoard[Idx(x, y)] == -1)
						continue;
					int Count = 0;
					for(int ny = y - 1; ny <= y + 1; ++ny)
					{
						for(int nx = x - 1; nx <= x + 1; ++nx)
						{
							if(nx < 0 || ny < 0 || nx >= s_Mines.m_W || ny >= s_Mines.m_H)
								continue;
							if(s_Mines.m_vBoard[Idx(nx, ny)] == -1)
								Count++;
						}
					}
					s_Mines.m_vBoard[Idx(x, y)] = Count;
				}
			}
		};

		auto RevealMines = [&](int StartX, int StartY) {
			if(StartX < 0 || StartY < 0 || StartX >= s_Mines.m_W || StartY >= s_Mines.m_H)
				return;
			const auto Idx = MinesIndex();
			const int StartIdx = Idx(StartX, StartY);
			if(s_Mines.m_vRevealed[StartIdx] || s_Mines.m_vFlagged[StartIdx])
				return;

			std::vector<ivec2> vStack;
			vStack.reserve(s_Mines.m_W * s_Mines.m_H);
			vStack.push_back(ivec2(StartX, StartY));
			while(!vStack.empty())
			{
				const ivec2 Cell = vStack.back();
				vStack.pop_back();
				const int X = Cell.x;
				const int Y = Cell.y;
				if(X < 0 || Y < 0 || X >= s_Mines.m_W || Y >= s_Mines.m_H)
					continue;
				const int CurIdx = Idx(X, Y);
				if(s_Mines.m_vRevealed[CurIdx] || s_Mines.m_vFlagged[CurIdx])
					continue;

				s_Mines.m_vRevealed[CurIdx] = 1;
				if(s_Mines.m_vBoard[CurIdx] == -1)
				{
					s_Mines.m_GameOver = true;
					continue;
				}
				s_Mines.m_RevealedCount++;
				if(s_Mines.m_vBoard[CurIdx] != 0)
					continue;
				for(int ny = Y - 1; ny <= Y + 1; ++ny)
					for(int nx = X - 1; nx <= X + 1; ++nx)
						if(nx != X || ny != Y)
							vStack.push_back(ivec2(nx, ny));
			}
		};

		auto RevealAllMines = [&]() {
			const auto Idx = MinesIndex();
			for(int y = 0; y < s_Mines.m_H; ++y)
				for(int x = 0; x < s_Mines.m_W; ++x)
					if(s_Mines.m_vBoard[Idx(x, y)] == -1)
						s_Mines.m_vRevealed[Idx(x, y)] = 1;
		};

			if(!s_Mines.m_Initialized)
				ResetMines();

			CUIRect TopBar, BoardArea;
			GameContent.HSplitTop(LINE_SIZE * 1.2f, &TopBar, &BoardArea);
			BoardArea.HSplitTop(MARGIN_SMALL, nullptr, &BoardArea);

			CUIRect Stats, BtnArea, RestartButton;
			TopBar.VSplitLeft(280.0f, &Stats, &BtnArea);
			BtnArea.VSplitRight(110.0f, &BtnArea, &RestartButton);

			char aStats[128];
			str_format(aStats, sizeof(aStats), "Bombs: %d   Flags: %d", s_Mines.m_Bombs, s_Mines.m_Flags);
			Ui()->DoLabel(&Stats, aStats, FONT_SIZE, TEXTALIGN_ML);
			static CButtonContainer s_MinesRestartButton;
			if(DoButton_Menu(&s_MinesRestartButton, TCLocalize("Restart"), 0, &RestartButton))
				ResetMines();

			const float CellSize = minimum(BoardArea.w / s_Mines.m_W, BoardArea.h / s_Mines.m_H);
			CUIRect Board;
			Board.w = CellSize * s_Mines.m_W;
			Board.h = CellSize * s_Mines.m_H;
			Board.x = BoardArea.x + (BoardArea.w - Board.w) / 2.0f;
			Board.y = BoardArea.y + (BoardArea.h - Board.h) / 2.0f;
			Board.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 4.0f);
			Board.DrawOutline(ColorRGBA(1.0f, 1.0f, 1.0f, 0.15f));

			const auto Idx = MinesIndex();
			int HoverX = -1;
			int HoverY = -1;
			if(Ui()->MouseInside(&Board))
			{
				const vec2 Mouse = Ui()->MousePos();
				HoverX = std::clamp((int)((Mouse.x - Board.x) / CellSize), 0, s_Mines.m_W - 1);
				HoverY = std::clamp((int)((Mouse.y - Board.y) / CellSize), 0, s_Mines.m_H - 1);
			}

			if(!s_Mines.m_GameOver && !s_Mines.m_Won && HoverX >= 0 && HoverY >= 0)
			{
				const int HoverIdx = Idx(HoverX, HoverY);
				if(Ui()->MouseButtonClicked(1) && !s_Mines.m_vRevealed[HoverIdx])
				{
					s_Mines.m_vFlagged[HoverIdx] = !s_Mines.m_vFlagged[HoverIdx];
					s_Mines.m_Flags += s_Mines.m_vFlagged[HoverIdx] ? 1 : -1;
				}
				if(Ui()->MouseButtonClicked(0))
				{
					if(s_Mines.m_vRevealed[HoverIdx] && s_Mines.m_vBoard[HoverIdx] > 0)
					{
						int FlaggedNeighbors = 0;
						std::vector<ivec2> vHiddenNeighbors;
						for(int ny = HoverY - 1; ny <= HoverY + 1; ++ny)
						{
							for(int nx = HoverX - 1; nx <= HoverX + 1; ++nx)
							{
								if(nx < 0 || ny < 0 || nx >= s_Mines.m_W || ny >= s_Mines.m_H || (nx == HoverX && ny == HoverY))
									continue;
								const int NIdx = Idx(nx, ny);
								if(s_Mines.m_vFlagged[NIdx])
									FlaggedNeighbors++;
								else if(!s_Mines.m_vRevealed[NIdx])
									vHiddenNeighbors.push_back(ivec2(nx, ny));
							}
						}
						if(FlaggedNeighbors == s_Mines.m_vBoard[HoverIdx])
							for(const ivec2 &Cell : vHiddenNeighbors)
								RevealMines(Cell.x, Cell.y);
					}
					else if(!s_Mines.m_vFlagged[HoverIdx])
					{
						if(s_Mines.m_FirstOpen)
						{
							GenerateMines(HoverX, HoverY);
							s_Mines.m_FirstOpen = false;
						}
						RevealMines(HoverX, HoverY);
					}

					if(s_Mines.m_GameOver)
						RevealAllMines();
				}
			}

			if(!s_Mines.m_GameOver && s_Mines.m_RevealedCount >= s_Mines.m_W * s_Mines.m_H - s_Mines.m_Bombs)
				s_Mines.m_Won = true;

			std::vector<uint8_t> vHighlight(s_Mines.m_W * s_Mines.m_H, 0);
			if(HoverX >= 0 && HoverY >= 0)
			{
				const int HoverIdx = Idx(HoverX, HoverY);
				if(s_Mines.m_vRevealed[HoverIdx] && s_Mines.m_vBoard[HoverIdx] > 0)
				{
					int FlaggedNeighbors = 0;
					std::vector<int> vHidden;
					for(int ny = HoverY - 1; ny <= HoverY + 1; ++ny)
					{
						for(int nx = HoverX - 1; nx <= HoverX + 1; ++nx)
						{
							if(nx < 0 || ny < 0 || nx >= s_Mines.m_W || ny >= s_Mines.m_H || (nx == HoverX && ny == HoverY))
								continue;
							const int NIdx = Idx(nx, ny);
							if(s_Mines.m_vFlagged[NIdx])
								FlaggedNeighbors++;
							else if(!s_Mines.m_vRevealed[NIdx])
								vHidden.push_back(NIdx);
						}
					}

					uint8_t HighlightType = 1;
					if(FlaggedNeighbors == s_Mines.m_vBoard[HoverIdx])
						HighlightType = 2;
					else if(FlaggedNeighbors > s_Mines.m_vBoard[HoverIdx])
						HighlightType = 3;
					for(int NIdx : vHidden)
						vHighlight[NIdx] = HighlightType;
				}
				else if(!s_Mines.m_vRevealed[HoverIdx])
				{
					vHighlight[HoverIdx] = 1;
				}
			}

			static const ColorRGBA s_aNumColors[9] = {
				ColorRGBA(1, 1, 1, 1),
				ColorRGBA(0.40f, 0.73f, 1.0f, 1.0f),
				ColorRGBA(0.41f, 0.88f, 0.48f, 1.0f),
				ColorRGBA(1.0f, 0.50f, 0.43f, 1.0f),
				ColorRGBA(0.86f, 0.54f, 1.0f, 1.0f),
				ColorRGBA(1.0f, 0.76f, 0.35f, 1.0f),
				ColorRGBA(0.35f, 0.95f, 0.85f, 1.0f),
				ColorRGBA(1.0f, 0.58f, 0.80f, 1.0f),
				ColorRGBA(0.86f, 0.86f, 0.86f, 1.0f)};

			for(int y = 0; y < s_Mines.m_H; ++y)
			{
				for(int x = 0; x < s_Mines.m_W; ++x)
				{
					const int CurIdx = Idx(x, y);
					CUIRect Cell;
					Cell.x = Board.x + x * CellSize;
					Cell.y = Board.y + y * CellSize;
					Cell.w = CellSize;
					Cell.h = CellSize;

					const bool Revealed = s_Mines.m_vRevealed[CurIdx];
					const bool Flagged = s_Mines.m_vFlagged[CurIdx];
					ColorRGBA BaseColor = Revealed ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f);
					if(vHighlight[CurIdx] == 1)
						BaseColor = BlendColors(BaseColor, ColorRGBA(0.25f, 0.65f, 1.0f, 0.22f), 0.7f);
					else if(vHighlight[CurIdx] == 2)
						BaseColor = BlendColors(BaseColor, ColorRGBA(0.35f, 1.0f, 0.45f, 0.22f), 0.8f);
					else if(vHighlight[CurIdx] == 3)
						BaseColor = BlendColors(BaseColor, ColorRGBA(1.0f, 0.35f, 0.35f, 0.22f), 0.8f);
					Cell.Draw(BaseColor, IGraphics::CORNER_NONE, 0.0f);
					Cell.DrawOutline(ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f));

					if(Revealed)
					{
						if(s_Mines.m_vBoard[CurIdx] == -1)
						{
							RenderIconLabel(Cell, FONT_ICON_BOMB, Cell.h * 0.68f, TEXTALIGN_MC);
						}
						else if(s_Mines.m_vBoard[CurIdx] > 0)
						{
							char aNum[8];
							str_format(aNum, sizeof(aNum), "%d", s_Mines.m_vBoard[CurIdx]);
							TextRender()->TextColor(s_aNumColors[s_Mines.m_vBoard[CurIdx]]);
							Ui()->DoLabel(&Cell, aNum, Cell.h * 0.55f, TEXTALIGN_MC);
							TextRender()->TextColor(TextRender()->DefaultTextColor());
						}
					}
					else if(Flagged)
					{
						RenderIconLabel(Cell, FONT_ICON_FLAG_CHECKERED, Cell.h * 0.6f, TEXTALIGN_MC);
					}
				}
			}

			if(s_Mines.m_GameOver || s_Mines.m_Won)
			{
				CUIRect Overlay = Board;
				Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_ALL, 4.0f);
				Ui()->DoLabel(&Overlay, s_Mines.m_Won ? TCLocalize("Victory") : TCLocalize("Game Over"), HEADLINE_FONT_SIZE, TEXTALIGN_MC);
			}
	}
	else if(s_SelectedGame == FUN_GAME_CHESS)
	{
		struct SChessMove
		{
			int m_FromX = 0;
			int m_FromY = 0;
			int m_ToX = 0;
			int m_ToY = 0;
		};

		struct SChessState
		{
			int m_Mode = 1; // 0 local, 1 bot
			int m_ShowMoves = 1;

			bool m_Initialized = false;
			char m_aBoard[8][8] = {{'.'}};
			bool m_WhiteTurn = true;
			bool m_GameOver = false;
			bool m_WhiteWon = false;
			bool m_Stalemate = false;
			bool m_FiftyMove = false;
			bool m_Threefold = false;
			int m_SelectedX = -1;
			int m_SelectedY = -1;
			bool m_Dragging = false;
			int m_DragFromX = -1;
			int m_DragFromY = -1;
			vec2 m_DragMouse = vec2(0.0f, 0.0f);
			bool m_HasMoveAnim = false;
			char m_AnimPiece = '.';
			int m_AnimFromX = -1;
			int m_AnimFromY = -1;
			int m_AnimToX = -1;
			int m_AnimToY = -1;
			float m_AnimStart = 0.0f;
			float m_AnimDuration = 0.18f;

			// castling variables
			bool m_WhiteKingSideCastle = true;
			bool m_WhiteQueenSideCastle = true;
			bool m_BlackKingSideCastle = true;
			bool m_BlackQueenSideCastle = true;

			// en passant variable
			int m_EnPassantColumn = -1;

			int m_HalfMoveClock = 0; // fifty-move rule

			std::unordered_map<std::string, int> m_RepetitionTable;
		};

		static SChessState s_Chess;

		auto IsWhitePiece = [](char Piece) { return Piece >= 'A' && Piece <= 'Z'; };
		auto IsBlackPiece = [](char Piece) { return Piece >= 'a' && Piece <= 'z'; };

		auto MakeStateKey = [&](const auto &Board) {
			std::string Key;

			// 1. board position
			for(int Y = 0; Y < 8; ++Y)
				for(int X = 0; X < 8; ++X)
					Key += Board[Y][X];

			// 2. turn
			Key += s_Chess.m_WhiteTurn ? 'w' : 'b';

			// 3. castling
			Key += s_Chess.m_WhiteKingSideCastle ? 'K' : '-';
			Key += s_Chess.m_WhiteQueenSideCastle ? 'Q' : '-';
			Key += s_Chess.m_BlackKingSideCastle ? 'k' : '-';
			Key += s_Chess.m_BlackQueenSideCastle ? 'q' : '-';

			// 4. en passant
			if(s_Chess.m_EnPassantColumn != -1)
				Key += ('a' + s_Chess.m_EnPassantColumn);
			else
				Key += '-';

			return Key;
		};

		auto ResetChess = [&]() {
			static const char *s_apSetup[8] = {
				"rnbqkbnr",
				"pppppppp",
				"........",
				"........",
				"........",
				"........",
				"PPPPPPPP",
				"RNBQKBNR"};
			s_Chess.m_Initialized = true;
			s_Chess.m_WhiteTurn = true;
			s_Chess.m_GameOver = false;
			s_Chess.m_WhiteWon = false;
			s_Chess.m_Stalemate = false;
			s_Chess.m_FiftyMove = false;
			s_Chess.m_Threefold = false;
			s_Chess.m_SelectedX = -1;
			s_Chess.m_SelectedY = -1;
			s_Chess.m_Dragging = false;
			s_Chess.m_DragFromX = -1;
			s_Chess.m_DragFromY = -1;
			s_Chess.m_HasMoveAnim = false;
			s_Chess.m_AnimPiece = '.';
			for(int y = 0; y < 8; ++y)
				for(int x = 0; x < 8; ++x)
					s_Chess.m_aBoard[y][x] = s_apSetup[y][x];

			s_Chess.m_WhiteKingSideCastle = true;
			s_Chess.m_WhiteQueenSideCastle = true;
			s_Chess.m_BlackKingSideCastle = true;
			s_Chess.m_BlackQueenSideCastle = true;

			s_Chess.m_EnPassantColumn = -1;

			s_Chess.m_HalfMoveClock = 0;

			s_Chess.m_RepetitionTable.clear();
			s_Chess.m_RepetitionTable[MakeStateKey(s_Chess.m_aBoard)] = 1;
		};

		using TChessBoard = std::array<std::array<char, 8>, 8>;

		auto CopyChessBoard = [&]() {
			TChessBoard Board{};
			for(int y = 0; y < 8; ++y)
				for(int x = 0; x < 8; ++x)
					Board[y][x] = s_Chess.m_aBoard[y][x];
			return Board;
		};

		auto CopyChessBoardFrom = [&](const auto &SourceBoard) {
			TChessBoard Board{};
			for(int Y = 0; Y < 8; ++Y)
				for(int X = 0; X < 8; ++X)
					Board[Y][X] = SourceBoard[Y][X];
			return Board;
		};

		auto IsPathClearOnBoard = [&](const auto &Board, int FromX, int FromY, int ToX, int ToY) {
			const int StepX = (ToX > FromX) - (ToX < FromX);
			const int StepY = (ToY > FromY) - (ToY < FromY);
			int X = FromX + StepX;
			int Y = FromY + StepY;
			while(X != ToX || Y != ToY)
			{
				if(Board[Y][X] != '.')
					return false;
				X += StepX;
				Y += StepY;
			}
			return true;
		};

		auto IsUnderAttack = [&](const auto &Board, int ToX, int ToY, bool WhiteTurn) {
			for(int FromY = 0; FromY < 8; ++FromY)
			{
				for(int FromX = 0; FromX < 8; ++FromX)
				{
					if(FromX == ToX && FromY == ToY)
						continue;

					const char Piece = Board[FromY][FromX];
					if(Piece == '.' || IsWhitePiece(Piece) == WhiteTurn)
						continue;

					const int Dx = ToX - FromX;
					const int Dy = ToY - FromY;
					const int AbsDx = abs(Dx);
					const int AbsDy = abs(Dy);
					const char UpperPiece = (char)toupper((unsigned char)Piece);

					if(UpperPiece == 'P')
					{
						const int Backward = WhiteTurn ? 1 : -1;
						if(Dy == Backward && AbsDx == 1)
							return true;
					}
					else if(UpperPiece == 'N')
					{
						if((AbsDx == 1 && AbsDy == 2) || (AbsDx == 2 && AbsDy == 1))
							return true;
					}
					else if(UpperPiece == 'B' || UpperPiece == 'R' || UpperPiece == 'Q')
					{
						bool IsValidLine = false;
						if(UpperPiece == 'B')
							IsValidLine = (AbsDx == AbsDy);
						else if(UpperPiece == 'R')
							IsValidLine = (Dx == 0 || Dy == 0);
						else
							IsValidLine = ((AbsDx == AbsDy) || (Dx == 0 || Dy == 0));
						if(IsValidLine)
						{
							if(IsPathClearOnBoard(Board, FromX, FromY, ToX, ToY))
								return true;

							const int StepX = (ToX > FromX) - (ToX < FromX);
							const int StepY = (ToY > FromY) - (ToY < FromY);
							int CheckX = FromX + StepX;
							int CheckY = FromY + StepY;
							bool FoundBlockPiece = false;
							while(CheckX != ToX || CheckY != ToY)
							{
								const char BlockPiece = Board[CheckY][CheckX];
								CheckX += StepX;
								CheckY += StepY;

								if(BlockPiece != '.')
								{
									if((char)toupper((unsigned char)BlockPiece) == 'K' && IsWhitePiece(BlockPiece) == WhiteTurn)
									{
										continue;
									}

									FoundBlockPiece = true;
									break;
								}
							}

							if(!FoundBlockPiece)
								return true;
						}
					}
					else if(UpperPiece == 'K')
					{
						if(AbsDx <= 1 && AbsDy <= 1)
							return true;
					}
				}
			}
			return false;
		};

		auto IsCheckOnBoard = [&](const auto &Board, bool WhiteTurn) {
			for(int y = 0; y < 8; ++y)
			{
				for(int x = 0; x < 8; ++x)
				{
					const char Piece = Board[y][x];
					if(Piece == '.' || IsWhitePiece(Piece) != WhiteTurn)
						continue;
					if((char)toupper((unsigned char)Piece) == 'K')
						return IsUnderAttack(Board, x, y, WhiteTurn);
				}
			}
			return false;
		};

		auto IsValidMoveOnBoard = [&](const auto &Board, int FromX, int FromY, int ToX, int ToY) {
			if(FromX == ToX && FromY == ToY)
				return false;
			if(ToX < 0 || ToY < 0 || ToX >= 8 || ToY >= 8)
				return false;

			const char Piece = Board[FromY][FromX];
			const char Target = Board[ToY][ToX];
			if(Piece == '.')
				return false;
			if(Target != '.' && IsWhitePiece(Piece) == IsWhitePiece(Target))
				return false;
			if(Target != '.' && (char)toupper((unsigned char)Target) == 'K')
				return false;

			const int Dx = ToX - FromX;
			const int Dy = ToY - FromY;
			const int AbsDx = abs(Dx);
			const int AbsDy = abs(Dy);
			const char UpperPiece = (char)toupper((unsigned char)Piece);

			bool IsBasicMoveValid = false;
			if(UpperPiece == 'P')
			{
				const int Forward = IsWhitePiece(Piece) ? -1 : 1;
				const int StartRow = IsWhitePiece(Piece) ? 6 : 1;
				if(Dx == 0 && Target == '.')
				{
					if(Dy == Forward)
						IsBasicMoveValid = true;
					else if(FromY == StartRow && Dy == Forward * 2 && Board[FromY + Forward][FromX] == '.')
						IsBasicMoveValid = true;
				}
				else if(AbsDx == 1 && Dy == Forward)
				{
					if(Target != '.' && IsWhitePiece(Target) != IsWhitePiece(Piece))
						IsBasicMoveValid = true;
					else if(Target == '.' && ToX == s_Chess.m_EnPassantColumn)
					{
						const int EnPassantRow = IsWhitePiece(Piece) ? 3 : 4;
						const char EnPassantVictim = Board[FromY][ToX];
						if(FromY == EnPassantRow && (char)toupper((unsigned char)EnPassantVictim) == 'P' && IsWhitePiece(EnPassantVictim) != IsWhitePiece(Piece))
							IsBasicMoveValid = true;
					}
				}
			}
			else if(UpperPiece == 'N')
				IsBasicMoveValid = (AbsDx == 1 && AbsDy == 2) || (AbsDx == 2 && AbsDy == 1);
			else if(UpperPiece == 'B')
				IsBasicMoveValid = AbsDx == AbsDy && IsPathClearOnBoard(Board, FromX, FromY, ToX, ToY);
			else if(UpperPiece == 'R')
				IsBasicMoveValid = (Dx == 0 || Dy == 0) && IsPathClearOnBoard(Board, FromX, FromY, ToX, ToY);
			else if(UpperPiece == 'Q')
				IsBasicMoveValid = ((AbsDx == AbsDy) || (Dx == 0 || Dy == 0)) && IsPathClearOnBoard(Board, FromX, FromY, ToX, ToY);
			else if(UpperPiece == 'K')
			{
				if(IsUnderAttack(Board, ToX, ToY, IsWhitePiece(Piece)))
					return false;

				if(AbsDx == 2 && AbsDy == 0)
				{
					const int KingStartY = IsWhitePiece(Piece) ? 7 : 0;
					if(FromX != 4 || FromY != KingStartY || ToY != KingStartY)
						return false;

					const int RookPos = Dx == 2 ? 7 : 0;
					const char ExpectedRook = IsWhitePiece(Piece) ? 'R' : 'r';
					if(Board[ToY][RookPos] != ExpectedRook)
						return false;
					if(!IsPathClearOnBoard(Board, FromX, FromY, RookPos, ToY))
						return false;
					if(IsUnderAttack(Board, FromX, FromY, IsWhitePiece(Piece)))
						return false;
					if(IsUnderAttack(Board, FromX + Dx / 2, FromY, IsWhitePiece(Piece)))
						return false;
					if(IsUnderAttack(Board, FromX + Dx, FromY, IsWhitePiece(Piece)))
						return false;

					if(Dx == 2)
					{
						IsBasicMoveValid = IsWhitePiece(Piece) ? s_Chess.m_WhiteKingSideCastle : s_Chess.m_BlackKingSideCastle;
					}
					else
					{
						IsBasicMoveValid = IsWhitePiece(Piece) ? s_Chess.m_WhiteQueenSideCastle : s_Chess.m_BlackQueenSideCastle;
					}
				}
				else
				{
					IsBasicMoveValid = AbsDx <= 1 && AbsDy <= 1;
				}
			}

			if(!IsBasicMoveValid)
				return false;

			// check check
			TChessBoard TempBoard = CopyChessBoardFrom(Board);
			TempBoard[ToY][ToX] = Piece;
			TempBoard[FromY][FromX] = '.';

			if(UpperPiece == 'P' && FromX != ToX && Target == '.')
			{
				const char EnPassantVictim = TempBoard[FromY][ToX];
				if((char)toupper((unsigned char)EnPassantVictim) == 'P' && IsWhitePiece(EnPassantVictim) != IsWhitePiece(Piece))
					TempBoard[FromY][ToX] = '.';
			}
			else if(UpperPiece == 'K' && AbsDx == 2)
			{
				const bool KingSide = ToX > FromX;
				const int RookFromX = KingSide ? 7 : 0;
				const int RookToX = KingSide ? 5 : 3;
				TempBoard[FromY][RookToX] = TempBoard[FromY][RookFromX];
				TempBoard[FromY][RookFromX] = '.';
			}

			if(IsCheckOnBoard(TempBoard, IsWhitePiece(Piece)))
				return false;

			return true;
		};

		auto IsCheckmateOnBoard = [&](const auto &Board, bool WhiteTurn) {
			if(!IsCheckOnBoard(Board, WhiteTurn))
				return false;
			for(int y = 0; y < 8; ++y)
			{
				for(int x = 0; x < 8; ++x)
				{
					const char Piece = Board[y][x];
					if(Piece == '.' || IsWhitePiece(Piece) != WhiteTurn)
						continue;
					for(int ty = 0; ty < 8; ++ty)
					{
						for(int tx = 0; tx < 8; ++tx)
						{
							if(IsValidMoveOnBoard(Board, x, y, tx, ty))
								return false;
						}
					}
				}
			}
			return true;
		};

		auto IsStalemateOnBoard = [&](const auto &Board, bool WhiteTurn) {
			if(IsCheckOnBoard(Board, WhiteTurn))
				return false;
			for(int y = 0; y < 8; ++y)
			{
				for(int x = 0; x < 8; ++x)
				{
					const char Piece = Board[y][x];
					if(Piece == '.' || IsWhitePiece(Piece) != WhiteTurn)
						continue;
					for(int ty = 0; ty < 8; ++ty)
					{
						for(int tx = 0; tx < 8; ++tx)
						{
							if(IsValidMoveOnBoard(Board, x, y, tx, ty))
								return false;
						}
					}
				}
			}
			return true;
		};

		auto IsThreefold = [&](const auto &Board) {
			const std::string Key = MakeStateKey(Board);
			return s_Chess.m_RepetitionTable[Key] >= 3;
		};

		auto CollectLegalMovesOnBoard = [&](const auto &Board, bool WhiteTurn) {
			std::vector<SChessMove> vMoves;
			for(int y = 0; y < 8; ++y)
			{
				for(int x = 0; x < 8; ++x)
				{
					const char Piece = Board[y][x];
					if(Piece == '.')
						continue;
					if(WhiteTurn != IsWhitePiece(Piece))
						continue;
					for(int ty = 0; ty < 8; ++ty)
					{
						for(int tx = 0; tx < 8; ++tx)
						{
							if(IsValidMoveOnBoard(Board, x, y, tx, ty))
								vMoves.push_back({x, y, tx, ty});
						}
					}
				}
			}
			return vMoves;
		};

		auto PieceValue = [&](char Piece) {
			switch((char)toupper((unsigned char)Piece))
			{
			case 'P': return 100;
			case 'N': return 320;
			case 'B': return 330;
			case 'R': return 500;
			case 'Q': return 900;
			case 'K': return 20000;
			default: return 0;
			}
		};

		auto ApplyMoveOnBoard = [&](auto &Board, const SChessMove &Move) {
			const char MovingPiece = Board[Move.m_FromY][Move.m_FromX];
			const char CapturedPiece = Board[Move.m_ToY][Move.m_ToX];
			const char Upper = (char)toupper((unsigned char)MovingPiece);

			Board[Move.m_ToY][Move.m_ToX] = MovingPiece;
			Board[Move.m_FromY][Move.m_FromX] = '.';

			// Pawn promotion
			if(Upper == 'P' && (Move.m_ToY == 0 || Move.m_ToY == 7))
				Board[Move.m_ToY][Move.m_ToX] = IsWhitePiece(MovingPiece) ? 'Q' : 'q';

			// Castling: move rook
			if(Upper == 'K' && abs(Move.m_ToX - Move.m_FromX) == 2)
			{
				const bool KingSide = Move.m_ToX > Move.m_FromX;
				const int RookFromX = KingSide ? 7 : 0;
				const int RookToX = KingSide ? 5 : 3;
				Board[Move.m_FromY][RookToX] = Board[Move.m_FromY][RookFromX];
				Board[Move.m_FromY][RookFromX] = '.';
			}

			// En passant: capture the pawn
			if(Upper == 'P' && Move.m_FromX != Move.m_ToX && CapturedPiece == '.')
			{
				const char EnPassantVictim = Board[Move.m_FromY][Move.m_ToX];
				if((char)toupper((unsigned char)EnPassantVictim) == 'P' && IsWhitePiece(EnPassantVictim) != IsWhitePiece(MovingPiece))
				{
					Board[Move.m_FromY][Move.m_ToX] = '.';
				}
			}

			return CapturedPiece;
		};

		auto EvaluateBoard = [&](const TChessBoard &Board) {
			bool WhiteKingAlive = false;
			bool BlackKingAlive = false;
			int Score = 0; // positive = better for black(bot)
			for(int y = 0; y < 8; ++y)
			{
				for(int x = 0; x < 8; ++x)
				{
					const char Piece = Board[y][x];
					if(Piece == '.')
						continue;

					const bool White = IsWhitePiece(Piece);
					if(Piece == 'K')
						WhiteKingAlive = true;
					else if(Piece == 'k')
						BlackKingAlive = true;

					int Local = PieceValue(Piece);
					const float CenterDist = absolute((float)x - 3.5f) + absolute((float)y - 3.5f);
					Local += round_to_int((3.5f - 0.5f * CenterDist) * 8.0f);

					const char Upper = (char)toupper((unsigned char)Piece);
					if(Upper == 'P')
						Local += White ? (6 - y) * 3 : (y - 1) * 3;
					else if(Upper == 'K')
						Local += White ? (y - 4) * 2 : (3 - y) * 2;

					Score += White ? -Local : Local;
				}
			}

			if(!BlackKingAlive)
				return -1000000;
			if(!WhiteKingAlive)
				return 1000000;
			return Score;
		};

		auto IsValidMove = [&](int FromX, int FromY, int ToX, int ToY) {
			return IsValidMoveOnBoard(s_Chess.m_aBoard, FromX, FromY, ToX, ToY);
		};

		auto PickBotMove = [&](SChessMove &OutMove) {
			const TChessBoard RootBoard = CopyChessBoard();
			std::vector<SChessMove> vMoves = CollectLegalMovesOnBoard(RootBoard, false);
			if(vMoves.empty())
				return false;

			int BestScore = -10000000;
			std::vector<SChessMove> vBestMoves;
			for(const SChessMove &Move : vMoves)
			{
				TChessBoard AfterBot = RootBoard;
				const char CapturedByBot = ApplyMoveOnBoard(AfterBot, Move);
				int Score = EvaluateBoard(AfterBot);
				if(CapturedByBot == 'K')
					Score = 1000000;
				else
				{
					std::vector<SChessMove> vReplies = CollectLegalMovesOnBoard(AfterBot, true);
					if(!vReplies.empty())
					{
						int WorstReplyScore = 10000000;
						for(const SChessMove &Reply : vReplies)
						{
							TChessBoard AfterReply = AfterBot;
							const char CapturedByWhite = ApplyMoveOnBoard(AfterReply, Reply);
							int ReplyScore = EvaluateBoard(AfterReply);
							if(CapturedByWhite == 'k')
								ReplyScore = -1000000;
							WorstReplyScore = std::min(WorstReplyScore, ReplyScore);
						}
						Score = WorstReplyScore;
					}
				}

				Score += PieceValue(CapturedByBot) / 5;
				Score += Move.m_ToY - Move.m_FromY;

				if(Score > BestScore + 10)
				{
					BestScore = Score;
					vBestMoves.clear();
					vBestMoves.push_back(Move);
				}
				else if(abs(Score - BestScore) <= 10)
				{
					vBestMoves.push_back(Move);
				}
			}

			OutMove = vBestMoves[rand() % vBestMoves.size()];
			return true;
		};

		auto ApplyChessMove = [&](const SChessMove &Move) {
			const char MovingPiece = s_Chess.m_aBoard[Move.m_FromY][Move.m_FromX];
			const char CapturedPiece = ApplyMoveOnBoard(s_Chess.m_aBoard, Move);
			s_Chess.m_HasMoveAnim = true;
			s_Chess.m_AnimPiece = MovingPiece;
			s_Chess.m_AnimFromX = Move.m_FromX;
			s_Chess.m_AnimFromY = Move.m_FromY;
			s_Chess.m_AnimToX = Move.m_ToX;
			s_Chess.m_AnimToY = Move.m_ToY;
			s_Chess.m_AnimStart = AnimTime;
			s_Chess.m_WhiteTurn = !s_Chess.m_WhiteTurn;

			if(MovingPiece == 'K')
			{
				s_Chess.m_WhiteKingSideCastle = false;
				s_Chess.m_WhiteQueenSideCastle = false;
			}
			else if(MovingPiece == 'k')
			{
				s_Chess.m_BlackKingSideCastle = false;
				s_Chess.m_BlackQueenSideCastle = false;
			}
			else if(MovingPiece == 'R')
			{
				if(Move.m_FromX == 0 && Move.m_FromY == 7)
					s_Chess.m_WhiteQueenSideCastle = false;
				else if(Move.m_FromX == 7 && Move.m_FromY == 7)
					s_Chess.m_WhiteKingSideCastle = false;
			}
			else if(MovingPiece == 'r')
			{
				if(Move.m_FromX == 0 && Move.m_FromY == 0)
					s_Chess.m_BlackQueenSideCastle = false;
				else if(Move.m_FromX == 7 && Move.m_FromY == 0)
					s_Chess.m_BlackKingSideCastle = false;
			}

			// disable castling if rook is captured
			if(CapturedPiece == 'R' && Move.m_ToX == 0 && Move.m_ToY == 7)
				s_Chess.m_WhiteQueenSideCastle = false;
			else if(CapturedPiece == 'R' && Move.m_ToX == 7 && Move.m_ToY == 7)
				s_Chess.m_WhiteKingSideCastle = false;
			else if(CapturedPiece == 'r' && Move.m_ToX == 0 && Move.m_ToY == 0)
				s_Chess.m_BlackQueenSideCastle = false;
			else if(CapturedPiece == 'r' && Move.m_ToX == 7 && Move.m_ToY == 0)
				s_Chess.m_BlackKingSideCastle = false;

			if((char)toupper((unsigned char)MovingPiece) == 'P' && abs(Move.m_FromY - Move.m_ToY) == 2)
			{
				const bool WhitePawn = IsWhitePiece(MovingPiece);
				bool CanEnPassant = false;

				// check left
				if(Move.m_ToX > 0)
				{
					const char LeftPiece = s_Chess.m_aBoard[Move.m_ToY][Move.m_ToX - 1];
					if((char)toupper((unsigned char)LeftPiece) == 'P' && IsWhitePiece(LeftPiece) != WhitePawn)
						CanEnPassant = true;
				}

				// check right
				if(Move.m_ToX < 7)
				{
					const char RightPiece = s_Chess.m_aBoard[Move.m_ToY][Move.m_ToX + 1];
					if((char)toupper((unsigned char)RightPiece) == 'P' && IsWhitePiece(RightPiece) != WhitePawn)
						CanEnPassant = true;
				}

				s_Chess.m_EnPassantColumn = CanEnPassant ? Move.m_ToX : -1;
			}
			else
			{
				s_Chess.m_EnPassantColumn = -1;
			}

			if(CapturedPiece != '.' || (char)toupper((unsigned char)MovingPiece) == 'P')
			{
				s_Chess.m_HalfMoveClock = 0;
				s_Chess.m_RepetitionTable.clear();
			}
			else
			{
				s_Chess.m_HalfMoveClock++;
			}

			const std::string Key = MakeStateKey(s_Chess.m_aBoard);
			s_Chess.m_RepetitionTable[Key]++;

			// end conditions
			if(IsCheckmateOnBoard(s_Chess.m_aBoard, s_Chess.m_WhiteTurn))
			{
				s_Chess.m_GameOver = true;
				s_Chess.m_WhiteWon = !s_Chess.m_WhiteTurn;
				s_Chess.m_Stalemate = false;
			}
			else if(IsStalemateOnBoard(s_Chess.m_aBoard, s_Chess.m_WhiteTurn))
			{
				s_Chess.m_GameOver = true;
				s_Chess.m_WhiteWon = false;
				s_Chess.m_Stalemate = true;
			}
			else if(s_Chess.m_HalfMoveClock >= 100)
			{
				s_Chess.m_GameOver = true;
				s_Chess.m_WhiteWon = false;
				s_Chess.m_FiftyMove = true;
			}
			else if(IsThreefold(s_Chess.m_aBoard))
			{
				s_Chess.m_GameOver = true;
				s_Chess.m_WhiteWon = false;
				s_Chess.m_Threefold = true;
			}
		};

			if(!s_Chess.m_Initialized)
				ResetChess();

			CUIRect TopBar, BoardArea;
			GameContent.HSplitTop(LINE_SIZE * 1.2f, &TopBar, &BoardArea);
			BoardArea.HSplitTop(MARGIN_SMALL, nullptr, &BoardArea);

			CUIRect TurnLabel, BtnArea, RestartButton;
			TopBar.VSplitLeft(320.0f, &TurnLabel, &BtnArea);
			BtnArea.VSplitRight(110.0f, &BtnArea, &RestartButton);

			const char *pStatus;
			if(s_Chess.m_GameOver)
			{
				if(s_Chess.m_Stalemate)
					pStatus = TCLocalize("Draw - Stalemate");
				else if(s_Chess.m_FiftyMove)
					pStatus = TCLocalize("Draw - Fifty move rule");
				else if(s_Chess.m_Threefold)
					pStatus = TCLocalize("Draw - Threefold repetition");
				else
					pStatus = s_Chess.m_WhiteWon ? TCLocalize("Winner: White") : TCLocalize("Winner: Black");
			}
			else
			{
				pStatus = s_Chess.m_WhiteTurn ? TCLocalize("Turn: White") : TCLocalize("Turn: Black");
			}
			Ui()->DoLabel(&TurnLabel, pStatus, FONT_SIZE, TEXTALIGN_ML);
			static CButtonContainer s_ChessRestartButton;
			if(DoButton_Menu(&s_ChessRestartButton, TCLocalize("Restart"), 0, &RestartButton))
				ResetChess();

			const float BoardSize = minimum(BoardArea.w, BoardArea.h);
			const float CellSize = BoardSize / 8.0f;
			CUIRect Board;
			Board.w = BoardSize;
			Board.h = BoardSize;
			Board.x = BoardArea.x + (BoardArea.w - Board.w) / 2.0f;
			Board.y = BoardArea.y + (BoardArea.h - Board.h) / 2.0f;
			Board.DrawOutline(ColorRGBA(1.0f, 1.0f, 1.0f, 0.22f));

			int HoverX = -1;
			int HoverY = -1;
			if(Ui()->MouseInside(&Board))
			{
				const vec2 Mouse = Ui()->MousePos();
				HoverX = std::clamp((int)((Mouse.x - Board.x) / CellSize), 0, 7);
				HoverY = std::clamp((int)((Mouse.y - Board.y) / CellSize), 0, 7);
			}

			if(s_Chess.m_HasMoveAnim && AnimTime - s_Chess.m_AnimStart >= s_Chess.m_AnimDuration)
				s_Chess.m_HasMoveAnim = false;

			if(!s_Chess.m_GameOver)
			{
				const bool BotTurn = s_Chess.m_Mode == 1 && !s_Chess.m_WhiteTurn;
				if(BotTurn && !s_Chess.m_Dragging)
				{
					SChessMove BotMove;
					if(!PickBotMove(BotMove))
					{
						s_Chess.m_GameOver = true;
						if(IsCheckOnBoard(s_Chess.m_aBoard, s_Chess.m_WhiteTurn))
						{
							s_Chess.m_WhiteWon = true;
							s_Chess.m_Stalemate = false;
						}
						else
						{
							s_Chess.m_WhiteWon = false;
							s_Chess.m_Stalemate = true;
						}
					}
					else
					{
						ApplyChessMove(BotMove);
					}
				}
				else if(!BotTurn)
				{
					if(HoverX >= 0 && HoverY >= 0 && Ui()->MouseButtonClicked(0))
					{
						const char ClickedPiece = s_Chess.m_aBoard[HoverY][HoverX];
						const bool OwnPiece = ClickedPiece != '.' && (s_Chess.m_WhiteTurn ? IsWhitePiece(ClickedPiece) : IsBlackPiece(ClickedPiece));
						if(OwnPiece)
						{
							s_Chess.m_Dragging = true;
							s_Chess.m_DragFromX = HoverX;
							s_Chess.m_DragFromY = HoverY;
							s_Chess.m_DragMouse = Ui()->MousePos();
							s_Chess.m_SelectedX = HoverX;
							s_Chess.m_SelectedY = HoverY;
						}
					}

					if(s_Chess.m_Dragging)
					{
						s_Chess.m_DragMouse = Ui()->MousePos();
						if(!Ui()->MouseButton(0))
						{
							const int FromX = s_Chess.m_DragFromX;
							const int FromY = s_Chess.m_DragFromY;
							if(HoverX >= 0 && HoverY >= 0)
							{
								const SChessMove Move = {FromX, FromY, HoverX, HoverY};
								if(!(HoverX == FromX && HoverY == FromY) && IsValidMove(Move.m_FromX, Move.m_FromY, Move.m_ToX, Move.m_ToY))
								{
									ApplyChessMove(Move);
									s_Chess.m_SelectedX = -1;
									s_Chess.m_SelectedY = -1;
								}
								else
								{
									const char HoverPiece = s_Chess.m_aBoard[HoverY][HoverX];
									const bool OwnPiece = HoverPiece != '.' && (s_Chess.m_WhiteTurn ? IsWhitePiece(HoverPiece) : IsBlackPiece(HoverPiece));
									s_Chess.m_SelectedX = OwnPiece ? HoverX : FromX;
									s_Chess.m_SelectedY = OwnPiece ? HoverY : FromY;
								}
							}
							else
							{
								s_Chess.m_SelectedX = FromX;
								s_Chess.m_SelectedY = FromY;
							}
							s_Chess.m_Dragging = false;
							s_Chess.m_DragFromX = -1;
							s_Chess.m_DragFromY = -1;
						}
					}
				}
			}

			const bool MoveAnimActive = s_Chess.m_HasMoveAnim && AnimTime - s_Chess.m_AnimStart < s_Chess.m_AnimDuration;

			for(int y = 0; y < 8; ++y)
			{
				for(int x = 0; x < 8; ++x)
				{
					CUIRect Cell;
					Cell.x = Board.x + x * CellSize;
					Cell.y = Board.y + y * CellSize;
					Cell.w = CellSize;
					Cell.h = CellSize;

					const bool LightSquare = ((x + y) % 2) == 0;
					Cell.Draw(LightSquare ? ColorRGBA(0.93f, 0.86f, 0.74f, 0.92f) : ColorRGBA(0.53f, 0.39f, 0.27f, 0.92f), IGraphics::CORNER_NONE, 0.0f);

					if(x == s_Chess.m_SelectedX && y == s_Chess.m_SelectedY)
						Cell.Draw(ColorRGBA(0.3f, 0.8f, 1.0f, 0.32f), IGraphics::CORNER_NONE, 0.0f);
					else if(x == HoverX && y == HoverY)
						Cell.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f), IGraphics::CORNER_NONE, 0.0f);
					else if(s_Chess.m_ShowMoves && s_Chess.m_SelectedX >= 0 && s_Chess.m_SelectedY >= 0 && IsValidMove(s_Chess.m_SelectedX, s_Chess.m_SelectedY, x, y))
						Cell.Draw(ColorRGBA(0.35f, 1.0f, 0.45f, 0.25f), IGraphics::CORNER_NONE, 0.0f);

					const bool SkipForDragging = s_Chess.m_Dragging && x == s_Chess.m_DragFromX && y == s_Chess.m_DragFromY;
					const bool SkipForAnim = MoveAnimActive && x == s_Chess.m_AnimToX && y == s_Chess.m_AnimToY;
					const char Piece = (SkipForDragging || SkipForAnim) ? '.' : s_Chess.m_aBoard[y][x];
					if(Piece != '.')
					{
						const char *pIcon = GetChessPieceIcon(Piece);
						if(pIcon)
						{
							const bool White = IsWhitePiece(Piece);
							const ColorRGBA PieceColor = White ? ColorRGBA(0.97f, 0.97f, 0.97f, 1.0f) : ColorRGBA(0.08f, 0.08f, 0.08f, 1.0f);
							RenderIconLabel(Cell, pIcon, Cell.h * 0.62f, TEXTALIGN_MC, &PieceColor);
						}
					}
				}
			}

			if(MoveAnimActive && s_Chess.m_AnimPiece != '.')
			{
				const float T = std::clamp((AnimTime - s_Chess.m_AnimStart) / s_Chess.m_AnimDuration, 0.0f, 1.0f);
				const float Ease = 1.0f - powf(1.0f - T, 3.0f);
				CUIRect AnimRect;
				AnimRect.w = CellSize;
				AnimRect.h = CellSize;
				AnimRect.x = Board.x + ((float)s_Chess.m_AnimFromX + (s_Chess.m_AnimToX - s_Chess.m_AnimFromX) * Ease) * CellSize;
				AnimRect.y = Board.y + ((float)s_Chess.m_AnimFromY + (s_Chess.m_AnimToY - s_Chess.m_AnimFromY) * Ease) * CellSize;
				const char *pIcon = GetChessPieceIcon(s_Chess.m_AnimPiece);
				if(pIcon)
				{
					const bool White = IsWhitePiece(s_Chess.m_AnimPiece);
					const ColorRGBA PieceColor = White ? ColorRGBA(0.97f, 0.97f, 0.97f, 0.98f) : ColorRGBA(0.08f, 0.08f, 0.08f, 0.98f);
					RenderIconLabel(AnimRect, pIcon, AnimRect.h * 0.62f, TEXTALIGN_MC, &PieceColor);
				}
			}

			if(s_Chess.m_Dragging && s_Chess.m_DragFromX >= 0 && s_Chess.m_DragFromY >= 0)
			{
				const char DragPiece = s_Chess.m_aBoard[s_Chess.m_DragFromY][s_Chess.m_DragFromX];
				const char *pIcon = GetChessPieceIcon(DragPiece);
				if(pIcon)
				{
					CUIRect DragRect;
					DragRect.w = CellSize;
					DragRect.h = CellSize;
					DragRect.x = s_Chess.m_DragMouse.x - DragRect.w * 0.5f;
					DragRect.y = s_Chess.m_DragMouse.y - DragRect.h * 0.5f;
					const bool White = IsWhitePiece(DragPiece);
					const ColorRGBA PieceColor = White ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.96f) : ColorRGBA(0.05f, 0.05f, 0.05f, 0.96f);
					RenderIconLabel(DragRect, pIcon, DragRect.h * 0.64f, TEXTALIGN_MC, &PieceColor);
				}
			}

			if(s_Chess.m_GameOver)
			{
				CUIRect Overlay = Board;
				Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.32f), IGraphics::CORNER_ALL, 4.0f);

				const char *pResult;
				if(s_Chess.m_Stalemate)
					pResult = TCLocalize("Draw - Stalemate");
				else if(s_Chess.m_FiftyMove)
					pResult = TCLocalize("Draw - Fifty move rule");
				else if(s_Chess.m_Threefold)
					pResult = TCLocalize("Draw - Threefold repetition");
				else
					pResult = s_Chess.m_WhiteWon ? TCLocalize("White Wins") : TCLocalize("Black Wins");

				Ui()->DoLabel(&Overlay, pResult, HEADLINE_FONT_SIZE, TEXTALIGN_MC);
			}
	}
	else if(s_SelectedGame == FUN_GAME_SUDOKU)
	{
		struct SSudokuState
		{
			int m_Difficulty = 1;
			int m_ShowConflicts = 1;

			bool m_Initialized = false;
			std::array<int, 81> m_aBoard{};
			std::array<int, 81> m_aSolution{};
			std::array<uint8_t, 81> m_aFixed{};
			std::array<uint8_t, 81> m_aConflict{};
			int m_SelectedX = -1;
			int m_SelectedY = -1;
			int m_Mistakes = 0;
			bool m_Won = false;
		};

		static SSudokuState s_Sudoku;

		auto SudokuIndex = [](int X, int Y) { return Y * 9 + X; };
		auto UpdateSudokuConflicts = [&]() {
			s_Sudoku.m_aConflict.fill(0);
			for(int y = 0; y < 9; ++y)
			{
				for(int x = 0; x < 9; ++x)
				{
					const int Idx = SudokuIndex(x, y);
					const int Value = s_Sudoku.m_aBoard[Idx];
					if(Value == 0)
						continue;
					for(int i = 0; i < 9; ++i)
					{
						if(i != x && s_Sudoku.m_aBoard[SudokuIndex(i, y)] == Value)
						{
							s_Sudoku.m_aConflict[Idx] = 1;
							s_Sudoku.m_aConflict[SudokuIndex(i, y)] = 1;
						}
						if(i != y && s_Sudoku.m_aBoard[SudokuIndex(x, i)] == Value)
						{
							s_Sudoku.m_aConflict[Idx] = 1;
							s_Sudoku.m_aConflict[SudokuIndex(x, i)] = 1;
						}
					}
					const int BoxX = (x / 3) * 3;
					const int BoxY = (y / 3) * 3;
					for(int by = BoxY; by < BoxY + 3; ++by)
						for(int bx = BoxX; bx < BoxX + 3; ++bx)
						{
							if((bx != x || by != y) && s_Sudoku.m_aBoard[SudokuIndex(bx, by)] == Value)
							{
								s_Sudoku.m_aConflict[Idx] = 1;
								s_Sudoku.m_aConflict[SudokuIndex(bx, by)] = 1;
							}
						}
				}
			}
		};

		auto ResetSudoku = [&]() {
			std::vector<int> vNums(9), vRowBands = {0, 1, 2}, vColBands = {0, 1, 2};
			std::iota(vNums.begin(), vNums.end(), 1);
			ShuffleVector(vNums);
			ShuffleVector(vRowBands);
			ShuffleVector(vColBands);

			std::vector<int> vRows, vCols;
			vRows.reserve(9);
			vCols.reserve(9);
			for(int Band : vRowBands)
			{
				std::vector<int> vInBand = {0, 1, 2};
				ShuffleVector(vInBand);
				for(int Row : vInBand)
					vRows.push_back(Band * 3 + Row);
			}
			for(int Band : vColBands)
			{
				std::vector<int> vInBand = {0, 1, 2};
				ShuffleVector(vInBand);
				for(int Col : vInBand)
					vCols.push_back(Band * 3 + Col);
			}

			for(int y = 0; y < 9; ++y)
			{
				for(int x = 0; x < 9; ++x)
				{
					const int Base = (vRows[y] * 3 + vRows[y] / 3 + vCols[x]) % 9;
					s_Sudoku.m_aSolution[SudokuIndex(x, y)] = vNums[Base];
				}
			}

			s_Sudoku.m_aBoard = s_Sudoku.m_aSolution;
			s_Sudoku.m_aFixed.fill(1);
			std::vector<int> vCells(81);
			std::iota(vCells.begin(), vCells.end(), 0);
			ShuffleVector(vCells);
			const int Blanks = s_Sudoku.m_Difficulty == 0 ? 35 : s_Sudoku.m_Difficulty == 1 ? 45 :
													  55;
			for(int i = 0; i < Blanks && i < (int)vCells.size(); ++i)
			{
				const int Idx = vCells[i];
				s_Sudoku.m_aBoard[Idx] = 0;
				s_Sudoku.m_aFixed[Idx] = 0;
			}

			s_Sudoku.m_Initialized = true;
			s_Sudoku.m_SelectedX = -1;
			s_Sudoku.m_SelectedY = -1;
			s_Sudoku.m_Mistakes = 0;
			s_Sudoku.m_Won = false;
			UpdateSudokuConflicts();
		};

		auto CheckSudokuWin = [&]() {
			for(int i = 0; i < 81; ++i)
			{
				if(s_Sudoku.m_aBoard[i] != s_Sudoku.m_aSolution[i])
					return false;
			}
			return true;
		};

			if(!s_Sudoku.m_Initialized)
				ResetSudoku();

			CUIRect TopBar, BoardArea;
			GameContent.HSplitTop(LINE_SIZE * 1.2f, &TopBar, &BoardArea);
			BoardArea.HSplitTop(MARGIN_SMALL, nullptr, &BoardArea);

			CUIRect Stats, BtnArea, RestartButton;
			TopBar.VSplitLeft(280.0f, &Stats, &BtnArea);
			BtnArea.VSplitRight(110.0f, &BtnArea, &RestartButton);

			char aStats[128];
			str_format(aStats, sizeof(aStats), "Mistakes: %d", s_Sudoku.m_Mistakes);
			Ui()->DoLabel(&Stats, aStats, FONT_SIZE, TEXTALIGN_ML);
			static CButtonContainer s_SudokuRestartButton;
			if(DoButton_Menu(&s_SudokuRestartButton, TCLocalize("Restart"), 0, &RestartButton))
				ResetSudoku();

			const float BoardSize = minimum(BoardArea.w, BoardArea.h);
			const float CellSize = BoardSize / 9.0f;
			CUIRect Board;
			Board.w = BoardSize;
			Board.h = BoardSize;
			Board.x = BoardArea.x + (BoardArea.w - Board.w) / 2.0f;
			Board.y = BoardArea.y + (BoardArea.h - Board.h) / 2.0f;
			Board.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.20f), IGraphics::CORNER_ALL, 4.0f);

			int HoverX = -1;
			int HoverY = -1;
			if(Ui()->MouseInside(&Board))
			{
				const vec2 Mouse = Ui()->MousePos();
				HoverX = std::clamp((int)((Mouse.x - Board.x) / CellSize), 0, 8);
				HoverY = std::clamp((int)((Mouse.y - Board.y) / CellSize), 0, 8);
			}
			if(HoverX >= 0 && HoverY >= 0 && Ui()->MouseButtonClicked(0))
			{
				s_Sudoku.m_SelectedX = HoverX;
				s_Sudoku.m_SelectedY = HoverY;
			}

			if(!s_Sudoku.m_Won && s_Sudoku.m_SelectedX >= 0 && s_Sudoku.m_SelectedY >= 0)
			{
				const int SelectedIdx = SudokuIndex(s_Sudoku.m_SelectedX, s_Sudoku.m_SelectedY);
				if(!s_Sudoku.m_aFixed[SelectedIdx])
				{
					int InputDigit = 0;
					for(int i = 1; i <= 9; ++i)
					{
						if(Input()->KeyPress(KEY_0 + i))
							InputDigit = i;
						if(Input()->KeyPress(KEY_KP_0 + i))
							InputDigit = i;
					}
					if(Input()->KeyPress(KEY_0) || Input()->KeyPress(KEY_KP_0) || Input()->KeyPress(KEY_BACKSPACE) || Input()->KeyPress(KEY_DELETE))
						InputDigit = -1;

					if(InputDigit != 0)
					{
						const int Old = s_Sudoku.m_aBoard[SelectedIdx];
						s_Sudoku.m_aBoard[SelectedIdx] = InputDigit < 0 ? 0 : InputDigit;
						if(InputDigit > 0 && InputDigit != s_Sudoku.m_aSolution[SelectedIdx] && Old != InputDigit)
							s_Sudoku.m_Mistakes++;
						UpdateSudokuConflicts();
						if(CheckSudokuWin())
							s_Sudoku.m_Won = true;
					}
				}
			}

			for(int y = 0; y < 9; ++y)
			{
				for(int x = 0; x < 9; ++x)
				{
					const int Idx = SudokuIndex(x, y);
					CUIRect Cell;
					Cell.x = Board.x + x * CellSize;
					Cell.y = Board.y + y * CellSize;
					Cell.w = CellSize;
					Cell.h = CellSize;

					ColorRGBA CellColor = s_Sudoku.m_aFixed[Idx] ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.05f);
					if(x == s_Sudoku.m_SelectedX && y == s_Sudoku.m_SelectedY)
						CellColor = BlendColors(CellColor, ColorRGBA(0.2f, 0.7f, 1.0f, 0.3f), 0.8f);
					else if(x == HoverX && y == HoverY)
						CellColor = BlendColors(CellColor, ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), 0.7f);
					if(s_Sudoku.m_ShowConflicts && s_Sudoku.m_aConflict[Idx])
						CellColor = BlendColors(CellColor, ColorRGBA(1.0f, 0.3f, 0.3f, 0.3f), 0.8f);

					Cell.Draw(CellColor, IGraphics::CORNER_NONE, 0.0f);

					const int Value = s_Sudoku.m_aBoard[Idx];
					if(Value > 0)
					{
						char aNum[8];
						str_format(aNum, sizeof(aNum), "%d", Value);
						if(s_Sudoku.m_aFixed[Idx])
							TextRender()->TextColor(ColorRGBA(0.93f, 0.93f, 0.93f, 1.0f));
						else
							TextRender()->TextColor(ColorRGBA(0.6f, 0.86f, 1.0f, 1.0f));
						Ui()->DoLabel(&Cell, aNum, Cell.h * 0.52f, TEXTALIGN_MC);
						TextRender()->TextColor(TextRender()->DefaultTextColor());
					}
				}
			}

			for(int i = 0; i <= 9; ++i)
			{
				const bool Thick = i % 3 == 0;
				const float Alpha = Thick ? 0.55f : 0.20f;
				const float Width = Thick ? 2.5f : 1.0f;
				Graphics()->TextureClear();
				Graphics()->LinesBegin();
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
				IGraphics::CLineItem VLine(Board.x + i * CellSize, Board.y, Board.x + i * CellSize, Board.y + Board.h);
				IGraphics::CLineItem HLine(Board.x, Board.y + i * CellSize, Board.x + Board.w, Board.y + i * CellSize);
				Graphics()->LinesDraw(&VLine, 1);
				Graphics()->LinesDraw(&HLine, 1);
				Graphics()->LinesEnd();
				if(Width > 1.0f)
				{
					Graphics()->QuadsBegin();
					Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
					IGraphics::CQuadItem VQuad(Board.x + i * CellSize - Width * 0.5f, Board.y, Width, Board.h);
					IGraphics::CQuadItem HQuad(Board.x, Board.y + i * CellSize - Width * 0.5f, Board.w, Width);
					Graphics()->QuadsDrawTL(&VQuad, 1);
					Graphics()->QuadsDrawTL(&HQuad, 1);
					Graphics()->QuadsEnd();
				}
			}

			if(s_Sudoku.m_Won)
			{
				CUIRect Overlay = Board;
				Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.30f), IGraphics::CORNER_ALL, 4.0f);
				Ui()->DoLabel(&Overlay, TCLocalize("Solved"), HEADLINE_FONT_SIZE, TEXTALIGN_MC);
			}
	}
	else if(s_SelectedGame == FUN_GAME_MEMORY)
	{
		struct SMemoryState
		{
			int m_SizePreset = 0; // 0 = 4x4, 1 = 6x6
			bool m_Initialized = false;
			int m_Size = 4;
			std::vector<int> m_vCards;
			std::vector<uint8_t> m_vRevealed;
			std::vector<uint8_t> m_vMatched;
			int m_FirstPick = -1;
			int m_SecondPick = -1;
			int64_t m_PendingResolveAt = 0;
			int m_Moves = 0;
			int m_PairsFound = 0;
			bool m_Won = false;
		};

		static SMemoryState s_Memory;

		auto SetMemoryPreset = [&]() {
			s_Memory.m_Size = s_Memory.m_SizePreset == 0 ? 4 : 6;
		};

		auto ResetMemory = [&]() {
			SetMemoryPreset();
			s_Memory.m_Initialized = true;
			s_Memory.m_FirstPick = -1;
			s_Memory.m_SecondPick = -1;
			s_Memory.m_PendingResolveAt = 0;
			s_Memory.m_Moves = 0;
			s_Memory.m_PairsFound = 0;
			s_Memory.m_Won = false;

			const int TotalCards = s_Memory.m_Size * s_Memory.m_Size;
			const int PairCount = TotalCards / 2;
			s_Memory.m_vCards.clear();
			s_Memory.m_vCards.reserve(TotalCards);
			for(int i = 1; i <= PairCount; ++i)
			{
				s_Memory.m_vCards.push_back(i);
				s_Memory.m_vCards.push_back(i);
			}
			ShuffleVector(s_Memory.m_vCards);
			s_Memory.m_vRevealed.assign(TotalCards, 0);
			s_Memory.m_vMatched.assign(TotalCards, 0);
		};

		auto ResolvePendingPair = [&]() {
			if(s_Memory.m_PendingResolveAt == 0 || time_get() < s_Memory.m_PendingResolveAt)
				return;

			if(s_Memory.m_FirstPick >= 0 && s_Memory.m_SecondPick >= 0)
			{
				const int A = s_Memory.m_FirstPick;
				const int B = s_Memory.m_SecondPick;
				if(s_Memory.m_vCards[A] == s_Memory.m_vCards[B])
				{
					s_Memory.m_vMatched[A] = 1;
					s_Memory.m_vMatched[B] = 1;
					s_Memory.m_PairsFound++;
					if(s_Memory.m_PairsFound * 2 >= (int)s_Memory.m_vCards.size())
						s_Memory.m_Won = true;
				}
				else
				{
					s_Memory.m_vRevealed[A] = 0;
					s_Memory.m_vRevealed[B] = 0;
				}
			}

			s_Memory.m_FirstPick = -1;
			s_Memory.m_SecondPick = -1;
			s_Memory.m_PendingResolveAt = 0;
		};

			if(!s_Memory.m_Initialized)
				ResetMemory();
			ResolvePendingPair();

			CUIRect TopBar, BoardArea;
			GameContent.HSplitTop(LINE_SIZE * 1.2f, &TopBar, &BoardArea);
			BoardArea.HSplitTop(MARGIN_SMALL, nullptr, &BoardArea);

			CUIRect StatsLabel, BtnArea, RestartButton;
			TopBar.VSplitLeft(300.0f, &StatsLabel, &BtnArea);
			BtnArea.VSplitRight(110.0f, &BtnArea, &RestartButton);

			char aStats[128];
			str_format(aStats, sizeof(aStats), "Moves: %d   Pairs: %d/%d", s_Memory.m_Moves, s_Memory.m_PairsFound, (int)s_Memory.m_vCards.size() / 2);
			Ui()->DoLabel(&StatsLabel, aStats, FONT_SIZE, TEXTALIGN_ML);
			static CButtonContainer s_MemoryRestartButton;
			if(DoButton_Menu(&s_MemoryRestartButton, TCLocalize("Restart"), 0, &RestartButton))
				ResetMemory();

			const float GridSize = minimum(BoardArea.w, BoardArea.h);
			const float CellSize = GridSize / s_Memory.m_Size;
			CUIRect Board;
			Board.w = GridSize;
			Board.h = GridSize;
			Board.x = BoardArea.x + (BoardArea.w - Board.w) / 2.0f;
			Board.y = BoardArea.y + (BoardArea.h - Board.h) / 2.0f;
			Board.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.22f), IGraphics::CORNER_ALL, 4.0f);

			int HoverX = -1;
			int HoverY = -1;
			if(Ui()->MouseInside(&Board))
			{
				const vec2 Mouse = Ui()->MousePos();
				HoverX = std::clamp((int)((Mouse.x - Board.x) / CellSize), 0, s_Memory.m_Size - 1);
				HoverY = std::clamp((int)((Mouse.y - Board.y) / CellSize), 0, s_Memory.m_Size - 1);
			}

			const bool PairPending = s_Memory.m_PendingResolveAt != 0;
			if(!s_Memory.m_Won && !PairPending && HoverX >= 0 && HoverY >= 0 && Ui()->MouseButtonClicked(0))
			{
				const int Idx = HoverY * s_Memory.m_Size + HoverX;
				if(!s_Memory.m_vMatched[Idx] && !s_Memory.m_vRevealed[Idx])
				{
					s_Memory.m_vRevealed[Idx] = 1;
					if(s_Memory.m_FirstPick < 0)
					{
						s_Memory.m_FirstPick = Idx;
					}
					else if(s_Memory.m_SecondPick < 0 && Idx != s_Memory.m_FirstPick)
					{
						s_Memory.m_SecondPick = Idx;
						s_Memory.m_Moves++;
						s_Memory.m_PendingResolveAt = time_get() + (int64_t)(time_freq() * 0.55f);
					}
				}
			}

			for(int y = 0; y < s_Memory.m_Size; ++y)
			{
				for(int x = 0; x < s_Memory.m_Size; ++x)
				{
					const int Idx = y * s_Memory.m_Size + x;
					const bool Revealed = s_Memory.m_vRevealed[Idx] != 0 || s_Memory.m_vMatched[Idx] != 0;

					CUIRect Cell;
					Cell.x = Board.x + x * CellSize;
					Cell.y = Board.y + y * CellSize;
					Cell.w = CellSize;
					Cell.h = CellSize;

					const float Pad = maximum(1.0f, CellSize * 0.06f);
					Cell.Margin(Pad, &Cell);

					ColorRGBA Col = Revealed ? ColorRGBA(0.32f, 0.64f, 0.93f, 0.90f) : ColorRGBA(0.17f, 0.17f, 0.2f, 0.95f);
					if(s_Memory.m_vMatched[Idx])
						Col = BlendColors(Col, ColorRGBA(0.25f, 0.95f, 0.5f, 0.95f), 0.55f);
					if(x == HoverX && y == HoverY)
						Col = BlendColors(Col, ColorRGBA(1.0f, 1.0f, 1.0f, 0.2f), 0.6f);
					Cell.Draw(Col, IGraphics::CORNER_ALL, 3.0f);

					if(Revealed)
					{
						char aValue[8];
						str_format(aValue, sizeof(aValue), "%d", s_Memory.m_vCards[Idx]);
						Ui()->DoLabel(&Cell, aValue, Cell.h * 0.46f, TEXTALIGN_MC);
					}
				}
			}

			if(s_Memory.m_Won)
			{
				CUIRect Overlay = Board;
				Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.28f), IGraphics::CORNER_ALL, 4.0f);
				Ui()->DoLabel(&Overlay, TCLocalize("All pairs found"), HEADLINE_FONT_SIZE, TEXTALIGN_MC);
			}
	}
	else if(s_SelectedGame == FUN_GAME_DICE3D)
	{
		struct SDieAngles
		{
			float m_Pitch = 0.0f;
			float m_Yaw = 0.0f;
			float m_Roll = 0.0f;
			float m_StartPitch = 0.0f;
			float m_StartYaw = 0.0f;
			float m_StartRoll = 0.0f;
			float m_TargetPitch = 0.0f;
			float m_TargetYaw = 0.0f;
			float m_TargetRoll = 0.0f;
		};

		struct SDice3DState
		{
			int m_DiceCount = 2;
			bool m_Initialized = false;
			bool m_Rolling = false;
			int64_t m_RollStart = 0;
			float m_RollDuration = 1.0f;
			SDieAngles m_aDice[2];
			int m_aTopValues[2] = {1, 1};
		};

		struct SVec3
		{
			float m_X = 0.0f;
			float m_Y = 0.0f;
			float m_Z = 0.0f;
		};

		struct SFaceDef
		{
			int m_aVerts[4];
			SVec3 m_Normal;
			SVec3 m_Center;
			SVec3 m_UAxis;
			SVec3 m_VAxis;
			int m_Value = 1;
			ColorRGBA m_Color;
		};

		static const SVec3 s_aCubeVerts[8] = {
			{-1.0f, -1.0f, -1.0f},
			{1.0f, -1.0f, -1.0f},
			{1.0f, 1.0f, -1.0f},
			{-1.0f, 1.0f, -1.0f},
			{-1.0f, -1.0f, 1.0f},
			{1.0f, -1.0f, 1.0f},
			{1.0f, 1.0f, 1.0f},
			{-1.0f, 1.0f, 1.0f}};

		static const SFaceDef s_aFaces[6] = {
			{{4, 5, 6, 7}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 1, ColorRGBA(0.98f, 0.98f, 0.99f, 1.0f)},
			{{0, 3, 2, 1}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 6, ColorRGBA(0.89f, 0.89f, 0.92f, 1.0f)},
			{{3, 7, 6, 2}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 2, ColorRGBA(0.94f, 0.94f, 0.97f, 1.0f)},
			{{0, 1, 5, 4}, {0.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 5, ColorRGBA(0.86f, 0.86f, 0.9f, 1.0f)},
			{{1, 2, 6, 5}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 3, ColorRGBA(0.92f, 0.92f, 0.95f, 1.0f)},
			{{0, 4, 7, 3}, {-1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, 4, ColorRGBA(0.88f, 0.88f, 0.92f, 1.0f)}};

		static SDice3DState s_Dice3D;
		static CButtonContainer s_DiceRollButton;

		auto Rotate3 = [&](const SVec3 &V, float Pitch, float Yaw, float Roll) {
			const float CX = cosf(Pitch);
			const float SX = sinf(Pitch);
			const float CY = cosf(Yaw);
			const float SY = sinf(Yaw);
			const float CZ = cosf(Roll);
			const float SZ = sinf(Roll);

			SVec3 R = V;
			R = {R.m_X, R.m_Y * CX - R.m_Z * SX, R.m_Y * SX + R.m_Z * CX};
			R = {R.m_X * CY + R.m_Z * SY, R.m_Y, -R.m_X * SY + R.m_Z * CY};
			R = {R.m_X * CZ - R.m_Y * SZ, R.m_X * SZ + R.m_Y * CZ, R.m_Z};
			return R;
		};

		auto Dot3 = [](const SVec3 &A, const SVec3 &B) {
			return A.m_X * B.m_X + A.m_Y * B.m_Y + A.m_Z * B.m_Z;
		};
		auto Add3 = [](const SVec3 &A, const SVec3 &B) {
			return SVec3{A.m_X + B.m_X, A.m_Y + B.m_Y, A.m_Z + B.m_Z};
		};
		auto Scale3 = [](const SVec3 &A, float S) {
			return SVec3{A.m_X * S, A.m_Y * S, A.m_Z * S};
		};

		auto Project = [&](const SVec3 &V, vec2 Center, float Scale) {
			const float Perspective = 3.6f / maximum(1.8f, 3.6f - V.m_Z * 0.72f);
			return vec2(Center.x + V.m_X * Scale * Perspective, Center.y - V.m_Y * Scale * Perspective);
		};

		auto RandomAngle = [&]() {
			return (rand() / (float)RAND_MAX) * (2.0f * pi);
		};

		auto GetTopValue = [&](const SDieAngles &Angles) {
			int TopValue = 1;
			float BestY = -1000000.0f;
			for(const SFaceDef &Face : s_aFaces)
			{
				const SVec3 RotatedNormal = Rotate3(Face.m_Normal, Angles.m_Pitch, Angles.m_Yaw, Angles.m_Roll);
				if(RotatedNormal.m_Y > BestY)
				{
					BestY = RotatedNormal.m_Y;
					TopValue = Face.m_Value;
				}
			}
			return TopValue;
		};

		auto SortFacePoints = [](const vec2 *pPointsIn, vec2 *pPointsOut) {
			vec2 Mid(0.0f, 0.0f);
			for(int i = 0; i < 4; ++i)
				Mid += pPointsIn[i];
			Mid *= 0.25f;

			int aOrder[4] = {0, 1, 2, 3};
			std::sort(aOrder, aOrder + 4, [&](int A, int B) {
				return atan2f(pPointsIn[A].y - Mid.y, pPointsIn[A].x - Mid.x) < atan2f(pPointsIn[B].y - Mid.y, pPointsIn[B].x - Mid.x);
			});
			for(int i = 0; i < 4; ++i)
				pPointsOut[i] = pPointsIn[aOrder[i]];
		};

		auto DrawFreeform = [&](const vec2 *pPoints, const ColorRGBA &Col) {
			Graphics()->TextureClear();
			Graphics()->TrianglesBegin();
			Graphics()->SetColor(Col.r, Col.g, Col.b, Col.a);
			IGraphics::CFreeformItem Quad(pPoints[0], pPoints[1], pPoints[2], pPoints[3]);
			Graphics()->QuadsDrawFreeform(&Quad, 1);
			Graphics()->TrianglesEnd();
		};

		auto DrawDie = [&](vec2 Center, float Scale, const SDieAngles &Angles) {
			CUIRect Shadow;
			Shadow.w = Scale * 1.55f;
			Shadow.h = Scale * 0.36f;
			Shadow.x = Center.x - Shadow.w * 0.5f;
			Shadow.y = Center.y + Scale * 0.85f;
			Shadow.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.18f), IGraphics::CORNER_ALL, Shadow.h * 0.5f);

			struct SDrawFace
			{
				int m_FaceIndex = 0;
				float m_Depth = 0.0f;
				SVec3 m_RotNormal;
				SVec3 m_RotCenter;
				SVec3 m_RotU;
				SVec3 m_RotV;
				vec2 m_aProjected[4];
			};

			std::vector<SDrawFace> vVisibleFaces;
			vVisibleFaces.reserve(3);
			for(int FaceIndex = 0; FaceIndex < (int)std::size(s_aFaces); ++FaceIndex)
			{
				const SFaceDef &Face = s_aFaces[FaceIndex];
				const SVec3 RotatedNormal = Rotate3(Face.m_Normal, Angles.m_Pitch, Angles.m_Yaw, Angles.m_Roll);
				if(RotatedNormal.m_Z <= 0.03f)
					continue;

				SDrawFace DrawFace;
				DrawFace.m_FaceIndex = FaceIndex;
				DrawFace.m_RotNormal = RotatedNormal;
				DrawFace.m_RotCenter = Rotate3(Face.m_Center, Angles.m_Pitch, Angles.m_Yaw, Angles.m_Roll);
				DrawFace.m_RotU = Rotate3(Face.m_UAxis, Angles.m_Pitch, Angles.m_Yaw, Angles.m_Roll);
				DrawFace.m_RotV = Rotate3(Face.m_VAxis, Angles.m_Pitch, Angles.m_Yaw, Angles.m_Roll);
				for(int i = 0; i < 4; ++i)
				{
					const SVec3 RotatedVertex = Rotate3(s_aCubeVerts[Face.m_aVerts[i]], Angles.m_Pitch, Angles.m_Yaw, Angles.m_Roll);
					DrawFace.m_aProjected[i] = Project(RotatedVertex, Center, Scale);
					DrawFace.m_Depth += RotatedVertex.m_Z;
				}
				DrawFace.m_Depth /= 4.0f;
				vVisibleFaces.push_back(DrawFace);
			}

			std::sort(vVisibleFaces.begin(), vVisibleFaces.end(), [](const SDrawFace &A, const SDrawFace &B) {
				return A.m_Depth < B.m_Depth;
			});

			for(const SDrawFace &DrawFace : vVisibleFaces)
			{
				const SFaceDef &Face = s_aFaces[DrawFace.m_FaceIndex];
				vec2 aOrdered[4];
				SortFacePoints(DrawFace.m_aProjected, aOrdered);

				const SVec3 LightDir = {0.33f, 0.76f, 0.56f};
				const float Light = std::clamp(0.46f + Dot3(DrawFace.m_RotNormal, LightDir) * 0.4f, 0.24f, 1.0f);
				const ColorRGBA FaceColor = ColorRGBA(Face.m_Color.r * Light, Face.m_Color.g * Light, Face.m_Color.b * Light, 1.0f);
				DrawFreeform(aOrdered, FaceColor);

				Graphics()->TextureClear();
				Graphics()->LinesBegin();
				Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.4f);
				IGraphics::CLineItem aLines[4] = {
					IGraphics::CLineItem(aOrdered[0].x, aOrdered[0].y, aOrdered[1].x, aOrdered[1].y),
					IGraphics::CLineItem(aOrdered[1].x, aOrdered[1].y, aOrdered[2].x, aOrdered[2].y),
					IGraphics::CLineItem(aOrdered[2].x, aOrdered[2].y, aOrdered[3].x, aOrdered[3].y),
					IGraphics::CLineItem(aOrdered[3].x, aOrdered[3].y, aOrdered[0].x, aOrdered[0].y)};
				Graphics()->LinesDraw(aLines, 4);
				Graphics()->LinesEnd();

				auto DrawPip = [&](float U, float V) {
					const float PipScale = 0.58f;
					const SVec3 PipOffset = Add3(Scale3(DrawFace.m_RotU, U * PipScale), Scale3(DrawFace.m_RotV, V * PipScale));
					const vec2 PipPos = Project(Add3(DrawFace.m_RotCenter, PipOffset), Center, Scale);
					const float PipRadius = maximum(1.6f, Scale * (0.055f + DrawFace.m_RotNormal.m_Z * 0.022f));
					CUIRect PipRect;
					PipRect.w = PipRadius * 2.0f;
					PipRect.h = PipRadius * 2.0f;
					PipRect.x = PipPos.x - PipRadius;
					PipRect.y = PipPos.y - PipRadius;
					PipRect.Draw(ColorRGBA(0.08f, 0.09f, 0.12f, 0.95f), IGraphics::CORNER_ALL, PipRadius);
				};

				const float D = 0.42f;
				switch(Face.m_Value)
				{
				case 1:
					DrawPip(0.0f, 0.0f);
					break;
				case 2:
					DrawPip(-D, -D);
					DrawPip(D, D);
					break;
				case 3:
					DrawPip(-D, -D);
					DrawPip(0.0f, 0.0f);
					DrawPip(D, D);
					break;
				case 4:
					DrawPip(-D, -D);
					DrawPip(D, -D);
					DrawPip(-D, D);
					DrawPip(D, D);
					break;
				case 5:
					DrawPip(-D, -D);
					DrawPip(D, -D);
					DrawPip(0.0f, 0.0f);
					DrawPip(-D, D);
					DrawPip(D, D);
					break;
				case 6:
					DrawPip(-D, -D);
					DrawPip(-D, 0.0f);
					DrawPip(-D, D);
					DrawPip(D, -D);
					DrawPip(D, 0.0f);
					DrawPip(D, D);
					break;
				default:
					break;
				}
			}
		};

		auto ResetDice3D = [&]() {
			s_Dice3D.m_Initialized = true;
			s_Dice3D.m_Rolling = false;
			for(int i = 0; i < 2; ++i)
			{
				s_Dice3D.m_aDice[i].m_Pitch = RandomAngle();
				s_Dice3D.m_aDice[i].m_Yaw = RandomAngle();
				s_Dice3D.m_aDice[i].m_Roll = RandomAngle();
				s_Dice3D.m_aTopValues[i] = GetTopValue(s_Dice3D.m_aDice[i]);
			}
		};

		auto StartDiceRoll = [&]() {
			s_Dice3D.m_Rolling = true;
			s_Dice3D.m_RollStart = time_get();
			for(int i = 0; i < s_Dice3D.m_DiceCount; ++i)
			{
				SDieAngles &Die = s_Dice3D.m_aDice[i];
				Die.m_StartPitch = Die.m_Pitch;
				Die.m_StartYaw = Die.m_Yaw;
				Die.m_StartRoll = Die.m_Roll;
				Die.m_TargetPitch = Die.m_StartPitch + ((6 + rand() % 5) * 2.0f * pi) + RandomAngle();
				Die.m_TargetYaw = Die.m_StartYaw + ((6 + rand() % 5) * 2.0f * pi) + RandomAngle();
				Die.m_TargetRoll = Die.m_StartRoll + ((7 + rand() % 6) * 2.0f * pi) + RandomAngle();
			}
		};

			if(!s_Dice3D.m_Initialized)
				ResetDice3D();

			CUIRect TopBar, BoardArea;
			GameContent.HSplitTop(LINE_SIZE * 1.2f, &TopBar, &BoardArea);
			BoardArea.HSplitTop(MARGIN_SMALL, nullptr, &BoardArea);

			CUIRect StatusLabel, ButtonsArea, RollButton, RestartButton;
			const float ButtonsWidth = minimum(430.0f, TopBar.w * 0.62f);
			TopBar.VSplitRight(ButtonsWidth, &StatusLabel, &ButtonsArea);
			ButtonsArea.VSplitLeft(8.0f, nullptr, &ButtonsArea);
			const float ButtonGap = 8.0f;
			const float ButtonW = maximum(1.0f, (ButtonsArea.w - ButtonGap * 2.0f) / 3.0f);
			ButtonsArea.VSplitLeft(ButtonW, &RollButton, &ButtonsArea);
			ButtonsArea.VSplitLeft(ButtonGap, nullptr, &ButtonsArea);
			ButtonsArea.VSplitLeft(ButtonW, &RestartButton, &ButtonsArea);
			ButtonsArea.VSplitLeft(ButtonGap, nullptr, &ButtonsArea);

			if(s_Dice3D.m_Rolling)
			{
				const int64_t Now = time_get();
				const float T = std::clamp((Now - s_Dice3D.m_RollStart) / (s_Dice3D.m_RollDuration * (float)time_freq()), 0.0f, 1.0f);
				const float Ease = 1.0f - powf(1.0f - T, 3.0f);
				for(int i = 0; i < s_Dice3D.m_DiceCount; ++i)
				{
					SDieAngles &Die = s_Dice3D.m_aDice[i];
					Die.m_Pitch = Die.m_StartPitch + (Die.m_TargetPitch - Die.m_StartPitch) * Ease;
					Die.m_Yaw = Die.m_StartYaw + (Die.m_TargetYaw - Die.m_StartYaw) * Ease;
					Die.m_Roll = Die.m_StartRoll + (Die.m_TargetRoll - Die.m_StartRoll) * Ease;
				}
				if(T >= 1.0f)
					s_Dice3D.m_Rolling = false;
			}

			for(int i = 0; i < s_Dice3D.m_DiceCount; ++i)
				s_Dice3D.m_aTopValues[i] = GetTopValue(s_Dice3D.m_aDice[i]);

			const int Sum = s_Dice3D.m_DiceCount == 1 ? s_Dice3D.m_aTopValues[0] : s_Dice3D.m_aTopValues[0] + s_Dice3D.m_aTopValues[1];
			char aStatus[128];
			if(s_Dice3D.m_DiceCount == 1)
				str_format(aStatus, sizeof(aStatus), s_Dice3D.m_Rolling ? "Rolling: %d" : "Result: %d", s_Dice3D.m_aTopValues[0]);
			else
				str_format(aStatus, sizeof(aStatus), s_Dice3D.m_Rolling ? "Rolling: %d + %d" : "Result: %d + %d = %d", s_Dice3D.m_aTopValues[0], s_Dice3D.m_aTopValues[1], Sum);
			Ui()->DoLabel(&StatusLabel, aStatus, FONT_SIZE, TEXTALIGN_ML);

			const bool RollHotkey = Input()->KeyPress(KEY_SPACE);
			if((DoButton_Menu(&s_DiceRollButton, TCLocalize("Roll"), 0, &RollButton) || RollHotkey) && !s_Dice3D.m_Rolling)
				StartDiceRoll();
			static CButtonContainer s_DiceRestartButton;
			if(DoButton_Menu(&s_DiceRestartButton, TCLocalize("Restart"), 0, &RestartButton))
				ResetDice3D();

			BoardArea.Draw(ColorRGBA(0.01f, 0.05f, 0.1f, 0.24f), IGraphics::CORNER_ALL, 8.0f);
			const float DrawAreaSize = minimum(BoardArea.w, BoardArea.h);
			const float DiceScale = s_Dice3D.m_DiceCount == 1 ? DrawAreaSize * 0.34f : DrawAreaSize * 0.28f;
			const vec2 Center(BoardArea.x + BoardArea.w * 0.5f, BoardArea.y + BoardArea.h * 0.5f);
			const float Separation = DiceScale * 2.45f;

			if(s_Dice3D.m_DiceCount == 1)
			{
				DrawDie(Center, DiceScale, s_Dice3D.m_aDice[0]);
				CUIRect ValueRect;
				ValueRect.w = 80.0f;
				ValueRect.h = 34.0f;
				ValueRect.x = Center.x - ValueRect.w * 0.5f;
				ValueRect.y = Center.y + DiceScale * 1.18f;
				ValueRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.18f), IGraphics::CORNER_ALL, 4.0f);
				char aValue[16];
				str_format(aValue, sizeof(aValue), "%d", s_Dice3D.m_aTopValues[0]);
				Ui()->DoLabel(&ValueRect, aValue, FONT_SIZE + 2.0f, TEXTALIGN_MC);
			}
			else
			{
				const vec2 LeftCenter(Center.x - Separation * 0.5f, Center.y);
				const vec2 RightCenter(Center.x + Separation * 0.5f, Center.y);
				DrawDie(LeftCenter, DiceScale, s_Dice3D.m_aDice[0]);
				DrawDie(RightCenter, DiceScale, s_Dice3D.m_aDice[1]);

				CUIRect LeftValue;
				LeftValue.w = 70.0f;
				LeftValue.h = 30.0f;
				LeftValue.x = LeftCenter.x - LeftValue.w * 0.5f;
				LeftValue.y = LeftCenter.y + DiceScale * 1.08f;
				LeftValue.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.16f), IGraphics::CORNER_ALL, 4.0f);
				char aLeft[16];
				str_format(aLeft, sizeof(aLeft), "%d", s_Dice3D.m_aTopValues[0]);
				Ui()->DoLabel(&LeftValue, aLeft, FONT_SIZE + 1.0f, TEXTALIGN_MC);

				CUIRect RightValue;
				RightValue.w = 70.0f;
				RightValue.h = 30.0f;
				RightValue.x = RightCenter.x - RightValue.w * 0.5f;
				RightValue.y = RightCenter.y + DiceScale * 1.08f;
				RightValue.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.16f), IGraphics::CORNER_ALL, 4.0f);
				char aRight[16];
				str_format(aRight, sizeof(aRight), "%d", s_Dice3D.m_aTopValues[1]);
				Ui()->DoLabel(&RightValue, aRight, FONT_SIZE + 1.0f, TEXTALIGN_MC);
			}
	}
	else if(s_SelectedGame == FUN_GAME_PONG)
	{
		struct SPongState
		{
			int m_Difficulty = 1;
			bool m_Initialized = false;
			float m_PlayerY = 0.5f;
			float m_BotY = 0.5f;
			vec2 m_BallPos = vec2(0.5f, 0.5f);
			vec2 m_BallVel = vec2(0.55f, 0.15f);
			int m_PlayerScore = 0;
			int m_BotScore = 0;
			int m_BestScore = 0;
			bool m_GameOver = false;
			bool m_Waiting = true;
			int64_t m_LastTick = 0;
		};

		static SPongState s_Pong;

		auto ResetPongRound = [&](int DirectionSign) {
			s_Pong.m_BallPos = vec2(0.5f, 0.5f);
			const float Angle = ((rand() / (float)RAND_MAX) * 0.7f - 0.35f);
			s_Pong.m_BallVel = vec2((DirectionSign >= 0 ? 1.0f : -1.0f) * (0.58f + s_Pong.m_Difficulty * 0.08f), Angle);
		};
		auto ResetPong = [&]() {
			s_Pong.m_Initialized = true;
			s_Pong.m_PlayerY = 0.5f;
			s_Pong.m_BotY = 0.5f;
			s_Pong.m_PlayerScore = 0;
			s_Pong.m_BotScore = 0;
			s_Pong.m_GameOver = false;
			s_Pong.m_Waiting = true;
			s_Pong.m_LastTick = time_get();
			ResetPongRound(rand() % 2 == 0 ? -1 : 1);
		};

			if(!s_Pong.m_Initialized)
				ResetPong();

			CUIRect TopBar, ArenaArea;
			GameContent.HSplitTop(LINE_SIZE * 1.2f, &TopBar, &ArenaArea);
			ArenaArea.HSplitTop(MARGIN_SMALL, nullptr, &ArenaArea);

			CUIRect Stats, BtnArea, RestartButton;
			TopBar.VSplitLeft(280.0f, &Stats, &BtnArea);
			BtnArea.VSplitRight(110.0f, &BtnArea, &RestartButton);

			char aStats[128];
			str_format(aStats, sizeof(aStats), "You %d : %d Bot", s_Pong.m_PlayerScore, s_Pong.m_BotScore);
			Ui()->DoLabel(&Stats, aStats, FONT_SIZE, TEXTALIGN_ML);
			static CButtonContainer s_PongRestartButton;
			if(DoButton_Menu(&s_PongRestartButton, TCLocalize("Restart"), 0, &RestartButton))
				ResetPong();

			CUIRect Arena = ArenaArea;
			const float PreferredW = minimum(ArenaArea.w, ArenaArea.h * 1.8f);
			Arena.w = PreferredW;
			Arena.h = PreferredW / 1.8f;
			Arena.x = ArenaArea.x + (ArenaArea.w - Arena.w) * 0.5f;
			Arena.y = ArenaArea.y + (ArenaArea.h - Arena.h) * 0.5f;
			Arena.Draw(ColorRGBA(0.02f, 0.05f, 0.09f, 0.92f), IGraphics::CORNER_ALL, 6.0f);
			Arena.DrawOutline(ColorRGBA(0.35f, 0.78f, 1.0f, 0.35f));

			{
				const bool AnyKey = Input()->KeyPress(KEY_W) || Input()->KeyPress(KEY_S) ||
					Input()->KeyPress(KEY_UP) || Input()->KeyPress(KEY_DOWN) ||
					Input()->KeyPress(KEY_SPACE) || Input()->KeyPress(KEY_RETURN) || Input()->KeyPress(KEY_KP_ENTER) ||
					(Ui()->MouseButtonClicked(0) && Ui()->MouseInside(&Arena));
				if(s_Pong.m_Waiting && AnyKey)
				{
					s_Pong.m_Waiting = false;
					s_Pong.m_LastTick = time_get();
				}
			}

			if(!s_Pong.m_Waiting && !s_Pong.m_GameOver)
			{
				const int64_t Now = time_get();
				float Dt = (Now - s_Pong.m_LastTick) / (float)time_freq();
				s_Pong.m_LastTick = Now;
				Dt = std::clamp(Dt, 0.0f, 0.04f);

				float PlayerDir = 0.0f;
				if(Input()->KeyIsPressed(KEY_W) || Input()->KeyIsPressed(KEY_UP))
					PlayerDir -= 1.0f;
				if(Input()->KeyIsPressed(KEY_S) || Input()->KeyIsPressed(KEY_DOWN))
					PlayerDir += 1.0f;

				const float PaddleHalf = 0.12f;
				const float PlayerSpeed = 1.2f;
				s_Pong.m_PlayerY = std::clamp(s_Pong.m_PlayerY + PlayerDir * PlayerSpeed * Dt, PaddleHalf, 1.0f - PaddleHalf);

				if(Ui()->MouseInside(&Arena))
				{
					const vec2 Mouse = Ui()->MousePos();
					const float LocalMouseY = (Mouse.y - Arena.y) / Arena.h;
					s_Pong.m_PlayerY = std::clamp(LocalMouseY, PaddleHalf, 1.0f - PaddleHalf);
				}

				const float BotSpeed = s_Pong.m_Difficulty == 0 ? 0.72f : s_Pong.m_Difficulty == 1 ? 0.95f :
														     1.18f;
				const float TargetY = s_Pong.m_BallPos.y + s_Pong.m_BallVel.y * (0.05f + s_Pong.m_Difficulty * 0.03f);
				if(s_Pong.m_BotY < TargetY)
					s_Pong.m_BotY = minimum(s_Pong.m_BotY + BotSpeed * Dt, 1.0f - PaddleHalf);
				else
					s_Pong.m_BotY = maximum(s_Pong.m_BotY - BotSpeed * Dt, PaddleHalf);

				s_Pong.m_BallPos += s_Pong.m_BallVel * Dt;
				const float BallRadius = 0.017f;
				if(s_Pong.m_BallPos.y <= BallRadius || s_Pong.m_BallPos.y >= 1.0f - BallRadius)
				{
					s_Pong.m_BallPos.y = std::clamp(s_Pong.m_BallPos.y, BallRadius, 1.0f - BallRadius);
					s_Pong.m_BallVel.y *= -1.0f;
				}

				const float PaddleXLeft = 0.05f;
				const float PaddleXRight = 0.95f;
				const float PaddleW = 0.015f;
				if(s_Pong.m_BallVel.x < 0.0f && s_Pong.m_BallPos.x - BallRadius <= PaddleXLeft + PaddleW)
				{
					if(fabs(s_Pong.m_BallPos.y - s_Pong.m_PlayerY) <= PaddleHalf)
					{
						s_Pong.m_BallPos.x = PaddleXLeft + PaddleW + BallRadius;
						const float HitFactor = (s_Pong.m_BallPos.y - s_Pong.m_PlayerY) / PaddleHalf;
						s_Pong.m_BallVel.x = fabs(s_Pong.m_BallVel.x) * 1.04f;
						s_Pong.m_BallVel.y = std::clamp(s_Pong.m_BallVel.y + HitFactor * 0.45f, -1.0f, 1.0f);
					}
				}
				if(s_Pong.m_BallVel.x > 0.0f && s_Pong.m_BallPos.x + BallRadius >= PaddleXRight - PaddleW)
				{
					if(fabs(s_Pong.m_BallPos.y - s_Pong.m_BotY) <= PaddleHalf)
					{
						s_Pong.m_BallPos.x = PaddleXRight - PaddleW - BallRadius;
						const float HitFactor = (s_Pong.m_BallPos.y - s_Pong.m_BotY) / PaddleHalf;
						s_Pong.m_BallVel.x = -fabs(s_Pong.m_BallVel.x) * 1.04f;
						s_Pong.m_BallVel.y = std::clamp(s_Pong.m_BallVel.y + HitFactor * 0.35f, -1.0f, 1.0f);
					}
				}

				if(s_Pong.m_BallPos.x < 0.0f)
				{
					s_Pong.m_BotScore++;
					ResetPongRound(1);
				}
				else if(s_Pong.m_BallPos.x > 1.0f)
				{
					s_Pong.m_PlayerScore++;
					s_Pong.m_BestScore = maximum(s_Pong.m_BestScore, s_Pong.m_PlayerScore);
					ResetPongRound(-1);
				}

				if(s_Pong.m_PlayerScore >= 7 || s_Pong.m_BotScore >= 7)
					s_Pong.m_GameOver = true;
			}

			const float CenterX = Arena.x + Arena.w * 0.5f;
			for(int i = 0; i < 14; ++i)
			{
				CUIRect Dash;
				Dash.w = 2.0f;
				Dash.h = Arena.h * 0.045f;
				Dash.x = CenterX - Dash.w * 0.5f;
				Dash.y = Arena.y + Arena.h * (0.03f + i * 0.069f);
				Dash.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.22f), IGraphics::CORNER_ALL, 1.0f);
			}

			const float PaddleHalf = Arena.h * 0.12f;
			const float PaddleW = Arena.w * 0.015f;
			CUIRect LeftPaddle;
			LeftPaddle.w = PaddleW;
			LeftPaddle.h = PaddleHalf * 2.0f;
			LeftPaddle.x = Arena.x + Arena.w * 0.05f;
			LeftPaddle.y = Arena.y + Arena.h * s_Pong.m_PlayerY - PaddleHalf;
			LeftPaddle.Draw(ColorRGBA(0.32f, 0.84f, 1.0f, 0.96f), IGraphics::CORNER_ALL, 3.0f);

			CUIRect RightPaddle;
			RightPaddle.w = PaddleW;
			RightPaddle.h = PaddleHalf * 2.0f;
			RightPaddle.x = Arena.x + Arena.w * 0.95f - PaddleW;
			RightPaddle.y = Arena.y + Arena.h * s_Pong.m_BotY - PaddleHalf;
			RightPaddle.Draw(ColorRGBA(1.0f, 0.58f, 0.42f, 0.96f), IGraphics::CORNER_ALL, 3.0f);

			const float BallRadiusPx = Arena.h * 0.018f;
			CUIRect BallTrail;
			BallTrail.w = BallRadiusPx * 1.9f;
			BallTrail.h = BallRadiusPx * 1.9f;
			BallTrail.x = Arena.x + Arena.w * (s_Pong.m_BallPos.x - s_Pong.m_BallVel.x * 0.03f) - BallTrail.w * 0.5f;
			BallTrail.y = Arena.y + Arena.h * (s_Pong.m_BallPos.y - s_Pong.m_BallVel.y * 0.03f) - BallTrail.h * 0.5f;
			BallTrail.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.18f), IGraphics::CORNER_ALL, BallTrail.h * 0.5f);

			CUIRect Ball;
			Ball.w = BallRadiusPx * 2.0f;
			Ball.h = BallRadiusPx * 2.0f;
			Ball.x = Arena.x + Arena.w * s_Pong.m_BallPos.x - Ball.w * 0.5f;
			Ball.y = Arena.y + Arena.h * s_Pong.m_BallPos.y - Ball.h * 0.5f;
			Ball.Draw(ColorRGBA(0.98f, 0.98f, 1.0f, 0.98f), IGraphics::CORNER_ALL, Ball.h * 0.5f);

			if(s_Pong.m_Waiting)
			{
				CUIRect Overlay = Arena;
				Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_ALL, 6.0f);
				Ui()->DoLabel(&Overlay, TCLocalize("Press any key to start"), HEADLINE_FONT_SIZE, TEXTALIGN_MC);
			}
			else if(s_Pong.m_GameOver)
			{
				CUIRect Overlay = Arena;
				Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.42f), IGraphics::CORNER_ALL, 6.0f);
				Ui()->DoLabel(&Overlay, s_Pong.m_PlayerScore > s_Pong.m_BotScore ? TCLocalize("You Win") : TCLocalize("Bot Wins"), HEADLINE_FONT_SIZE, TEXTALIGN_MC);
			}
	}
	else if(s_SelectedGame == FUN_GAME_DINOSAUR)
	{
		struct SDinoObstacle
		{
			float m_X = 0.0f;
			float m_W = 0.05f;
			float m_H = 0.12f;
		};

		struct SDinosaurState
		{
			int m_Difficulty = 1;
			bool m_Initialized = false;
			float m_DinoY = 0.0f;
			float m_DinoVel = 0.0f;
			std::vector<SDinoObstacle> m_vObstacles;
			float m_SpawnTimer = 0.0f;
			float m_Score = 0.0f;
			int m_BestScore = 0;
			bool m_GameOver = false;
			int64_t m_LastTick = 0;
		};

		static SDinosaurState s_Dino;

		auto DinoRandom = [&](float Min, float Max) {
			return Min + (rand() / (float)RAND_MAX) * (Max - Min);
		};
		auto ResetDino = [&]() {
			s_Dino.m_Initialized = true;
			s_Dino.m_DinoY = 0.0f;
			s_Dino.m_DinoVel = 0.0f;
			s_Dino.m_vObstacles.clear();
			s_Dino.m_SpawnTimer = 0.7f;
			s_Dino.m_Score = 0.0f;
			s_Dino.m_GameOver = false;
			s_Dino.m_LastTick = time_get();
		};

			if(!s_Dino.m_Initialized)
				ResetDino();

			CUIRect TopBar, ArenaArea;
			GameContent.HSplitTop(LINE_SIZE * 1.2f, &TopBar, &ArenaArea);
			ArenaArea.HSplitTop(MARGIN_SMALL, nullptr, &ArenaArea);

			CUIRect Stats, BtnArea, RestartButton;
			TopBar.VSplitLeft(280.0f, &Stats, &BtnArea);
			BtnArea.VSplitRight(110.0f, &BtnArea, &RestartButton);

			const int CurrentScore = round_to_int(s_Dino.m_Score);
			char aStats[128];
			str_format(aStats, sizeof(aStats), "Score: %d   Best: %d", CurrentScore, s_Dino.m_BestScore);
			Ui()->DoLabel(&Stats, aStats, FONT_SIZE, TEXTALIGN_ML);
			static CButtonContainer s_DinoRestartButton;
			if(DoButton_Menu(&s_DinoRestartButton, TCLocalize("Restart"), 0, &RestartButton))
				ResetDino();

			CUIRect Arena = ArenaArea;
			const float PreferredW = minimum(ArenaArea.w, ArenaArea.h * 2.4f);
			Arena.w = PreferredW;
			Arena.h = PreferredW / 2.4f;
			Arena.x = ArenaArea.x + (ArenaArea.w - Arena.w) * 0.5f;
			Arena.y = ArenaArea.y + (ArenaArea.h - Arena.h) * 0.5f;
			Arena.Draw(ColorRGBA(0.94f, 0.93f, 0.88f, 0.96f), IGraphics::CORNER_ALL, 6.0f);

			const bool JumpPressed = Input()->KeyPress(KEY_SPACE) || Input()->KeyPress(KEY_W) || Input()->KeyPress(KEY_UP) || (Ui()->MouseInside(&Arena) && Ui()->MouseButtonClicked(0));
			const float GroundY = 0.82f;
			const float DinoX = 0.18f;
			const float DinoW = 0.07f;
			const float DinoH = 0.14f;
			const float Gravity = 3.2f;

			if(!s_Dino.m_GameOver)
			{
				const int64_t Now = time_get();
				float Dt = (Now - s_Dino.m_LastTick) / (float)time_freq();
				s_Dino.m_LastTick = Now;
				Dt = std::clamp(Dt, 0.0f, 0.05f);

				if(JumpPressed && s_Dino.m_DinoY <= 0.001f)
					s_Dino.m_DinoVel = 1.25f;

				s_Dino.m_DinoVel -= Gravity * Dt;
				s_Dino.m_DinoY += s_Dino.m_DinoVel * Dt;
				if(s_Dino.m_DinoY < 0.0f)
				{
					s_Dino.m_DinoY = 0.0f;
					s_Dino.m_DinoVel = 0.0f;
				}

				const float SpeedBase = s_Dino.m_Difficulty == 0 ? 0.55f : s_Dino.m_Difficulty == 1 ? 0.68f :
														      0.82f;
				const float ScrollSpeed = minimum(1.55f, SpeedBase + s_Dino.m_Score * 0.0045f);
				s_Dino.m_Score += Dt * 10.0f * ScrollSpeed;
				s_Dino.m_BestScore = maximum(s_Dino.m_BestScore, round_to_int(s_Dino.m_Score));

				s_Dino.m_SpawnTimer -= Dt * ScrollSpeed;
				if(s_Dino.m_SpawnTimer <= 0.0f)
				{
					SDinoObstacle Obstacle;
					Obstacle.m_X = 1.12f;
					Obstacle.m_W = DinoRandom(0.038f, 0.07f);
					Obstacle.m_H = DinoRandom(0.09f, 0.17f);
					s_Dino.m_vObstacles.push_back(Obstacle);
					s_Dino.m_SpawnTimer = DinoRandom(0.95f, 1.55f);
				}

				for(SDinoObstacle &Obstacle : s_Dino.m_vObstacles)
					Obstacle.m_X -= ScrollSpeed * Dt;
				s_Dino.m_vObstacles.erase(std::remove_if(s_Dino.m_vObstacles.begin(), s_Dino.m_vObstacles.end(), [](const SDinoObstacle &Obstacle) {
					return Obstacle.m_X < -0.2f;
				}),
					s_Dino.m_vObstacles.end());

				const float DinoBottom = GroundY - s_Dino.m_DinoY;
				const float DinoLeft = DinoX - DinoW * 0.5f;
				const float DinoRight = DinoX + DinoW * 0.5f;
				const float DinoTop = DinoBottom - DinoH;

				for(const SDinoObstacle &Obstacle : s_Dino.m_vObstacles)
				{
					const float ObstacleLeft = Obstacle.m_X - Obstacle.m_W * 0.5f;
					const float ObstacleRight = Obstacle.m_X + Obstacle.m_W * 0.5f;
					const float ObstacleTop = GroundY - Obstacle.m_H;
					const bool OverlapX = DinoRight > ObstacleLeft && DinoLeft < ObstacleRight;
					const bool OverlapY = DinoBottom > ObstacleTop && DinoTop < GroundY;
					if(OverlapX && OverlapY)
					{
						s_Dino.m_GameOver = true;
						break;
					}
				}
			}
			else
			{
				s_Dino.m_LastTick = time_get();
				if(JumpPressed)
					ResetDino();
			}

			CUIRect Ground;
			Ground.x = Arena.x;
			Ground.w = Arena.w;
			Ground.h = Arena.h * 0.08f;
			Ground.y = Arena.y + Arena.h * GroundY;
			Ui()->ClipEnable(&Arena);
			Ground.Draw(ColorRGBA(0.72f, 0.66f, 0.48f, 0.95f), IGraphics::CORNER_ALL, 4.0f);

			for(int i = 0; i < 12; ++i)
			{
				CUIRect Mark;
				Mark.w = Arena.w * 0.035f;
				Mark.h = 2.0f;
				Mark.x = Arena.x + fmodf(AnimTime * 100.0f + i * Arena.w * 0.12f, Arena.w + Mark.w) - Mark.w;
				Mark.y = Ground.y + Ground.h * 0.5f;
				Mark.Draw(ColorRGBA(0.45f, 0.41f, 0.3f, 0.55f), IGraphics::CORNER_ALL, 1.0f);
			}

			CUIRect Dino;
			Dino.w = Arena.w * DinoW;
			Dino.h = Arena.h * DinoH;
			Dino.x = Arena.x + Arena.w * (DinoX - DinoW * 0.5f);
			Dino.y = Arena.y + Arena.h * (GroundY - s_Dino.m_DinoY - DinoH);
			Dino.Draw(ColorRGBA(0.22f, 0.25f, 0.29f, 0.98f), IGraphics::CORNER_ALL, 4.0f);
			const ColorRGBA DinoIconColor(0.95f, 0.95f, 0.95f, 0.88f);
			RenderIconLabel(Dino, FONT_ICON_DRAGON, Dino.h * 0.58f, TEXTALIGN_MC, &DinoIconColor);

			for(const SDinoObstacle &Obstacle : s_Dino.m_vObstacles)
			{
				CUIRect Cactus;
				Cactus.w = Arena.w * Obstacle.m_W;
				Cactus.h = Arena.h * Obstacle.m_H;
				Cactus.x = Arena.x + Arena.w * (Obstacle.m_X - Obstacle.m_W * 0.5f);
				Cactus.y = Arena.y + Arena.h * (GroundY - Obstacle.m_H);
				Cactus.Draw(ColorRGBA(0.24f, 0.62f, 0.27f, 0.96f), IGraphics::CORNER_ALL, 2.5f);
			}
			Ui()->ClipDisable();

			if(s_Dino.m_GameOver)
			{
				CUIRect Overlay = Arena;
				Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.32f), IGraphics::CORNER_ALL, 6.0f);
				Ui()->DoLabel(&Overlay, TCLocalize("You hit a cactus"), HEADLINE_FONT_SIZE, TEXTALIGN_MC);
			}
	}
	else if(s_SelectedGame == FUN_GAME_BRICK_BREAKER)
	{
		struct SBrickBreakerState
		{
			int m_RowPreset = 1;
			int m_LivesPreset = 1;
			bool m_Initialized = false;
			int m_Rows = 6;
			int m_Cols = 10;
			int m_LivesStart = 3;
			int m_Lives = 3;
			int m_Score = 0;
			int m_BestScore = 0;
			std::vector<uint8_t> m_vBricks;
			int m_RemainingBricks = 0;
			float m_PaddleX = 0.5f;
			float m_PaddleW = 0.18f;
			vec2 m_BallPos = vec2(0.5f, 0.82f);
			vec2 m_BallVel = vec2(0.0f, 0.0f);
			bool m_BallLaunched = false;
			bool m_GameOver = false;
			bool m_Won = false;
			int64_t m_LastTick = 0;
		};

		static SBrickBreakerState s_Brick;

		auto BrickIdx = [&](int X, int Y) {
			return Y * s_Brick.m_Cols + X;
		};
		auto ApplyBrickPreset = [&]() {
			s_Brick.m_Rows = s_Brick.m_RowPreset == 0 ? 4 : s_Brick.m_RowPreset == 1 ? 6 :
												   8;
			s_Brick.m_LivesStart = s_Brick.m_LivesPreset == 0 ? 2 : s_Brick.m_LivesPreset == 1 ? 3 :
													     5;
		};
		auto ResetBrickRound = [&]() {
			s_Brick.m_PaddleX = 0.5f;
			s_Brick.m_BallPos = vec2(s_Brick.m_PaddleX, 0.84f);
			s_Brick.m_BallVel = vec2(0.0f, 0.0f);
			s_Brick.m_BallLaunched = false;
		};
		auto ResetBrickBreaker = [&]() {
			ApplyBrickPreset();
			s_Brick.m_Initialized = true;
			s_Brick.m_Lives = s_Brick.m_LivesStart;
			s_Brick.m_Score = 0;
			s_Brick.m_GameOver = false;
			s_Brick.m_Won = false;
			s_Brick.m_Cols = 10;
			s_Brick.m_vBricks.assign((size_t)s_Brick.m_Cols * s_Brick.m_Rows, 1);
			s_Brick.m_RemainingBricks = (int)s_Brick.m_vBricks.size();
			s_Brick.m_LastTick = time_get();
			ResetBrickRound();
		};

			if(!s_Brick.m_Initialized)
				ResetBrickBreaker();

			CUIRect TopBar, ArenaArea;
			GameContent.HSplitTop(LINE_SIZE * 1.2f, &TopBar, &ArenaArea);
			ArenaArea.HSplitTop(MARGIN_SMALL, nullptr, &ArenaArea);

			CUIRect Stats, BtnArea, RestartButton;
			TopBar.VSplitLeft(380.0f, &Stats, &BtnArea);
			BtnArea.VSplitRight(110.0f, &BtnArea, &RestartButton);

			char aStats[160];
			str_format(aStats, sizeof(aStats), "Score: %d   Bricks: %d   Lives: %d   Best: %d", s_Brick.m_Score, s_Brick.m_RemainingBricks, s_Brick.m_Lives, s_Brick.m_BestScore);
			Ui()->DoLabel(&Stats, aStats, FONT_SIZE, TEXTALIGN_ML);
			static CButtonContainer s_BrickRestartButton;
			if(DoButton_Menu(&s_BrickRestartButton, TCLocalize("Restart"), 0, &RestartButton))
				ResetBrickBreaker();

			CUIRect Arena = ArenaArea;
			const float PreferredW = minimum(ArenaArea.w, ArenaArea.h * 1.35f);
			Arena.w = PreferredW;
			Arena.h = PreferredW / 1.35f;
			Arena.x = ArenaArea.x + (ArenaArea.w - Arena.w) * 0.5f;
			Arena.y = ArenaArea.y + (ArenaArea.h - Arena.h) * 0.5f;
			Arena.Draw(ColorRGBA(0.03f, 0.07f, 0.14f, 0.94f), IGraphics::CORNER_ALL, 6.0f);
			Arena.DrawOutline(ColorRGBA(0.35f, 0.72f, 1.0f, 0.30f));

			if(!s_Brick.m_GameOver && !s_Brick.m_Won)
			{
				const int64_t Now = time_get();
				float Dt = (Now - s_Brick.m_LastTick) / (float)time_freq();
				s_Brick.m_LastTick = Now;
				Dt = std::clamp(Dt, 0.0f, 0.05f);

				float PaddleDir = 0.0f;
				if(Input()->KeyIsPressed(KEY_LEFT) || Input()->KeyIsPressed(KEY_A))
					PaddleDir -= 1.0f;
				if(Input()->KeyIsPressed(KEY_RIGHT) || Input()->KeyIsPressed(KEY_D))
					PaddleDir += 1.0f;
				s_Brick.m_PaddleX = std::clamp(s_Brick.m_PaddleX + PaddleDir * Dt * 0.9f, s_Brick.m_PaddleW * 0.5f, 1.0f - s_Brick.m_PaddleW * 0.5f);

				if(Ui()->MouseInside(&Arena))
				{
					const vec2 Mouse = Ui()->MousePos();
					s_Brick.m_PaddleX = std::clamp((Mouse.x - Arena.x) / Arena.w, s_Brick.m_PaddleW * 0.5f, 1.0f - s_Brick.m_PaddleW * 0.5f);
				}

				const float BallRadius = 0.015f;
				if(!s_Brick.m_BallLaunched)
				{
					s_Brick.m_BallPos = vec2(s_Brick.m_PaddleX, 0.84f);
					const bool Launch = Input()->KeyPress(KEY_SPACE) || Input()->KeyPress(KEY_UP) || (Ui()->MouseInside(&Arena) && Ui()->MouseButtonClicked(0));
					if(Launch)
					{
						const float Side = (rand() / (float)RAND_MAX) * 0.8f - 0.4f;
						s_Brick.m_BallVel = vec2(Side, -(0.74f + s_Brick.m_RowPreset * 0.08f));
						s_Brick.m_BallLaunched = true;
					}
				}
				else
				{
					float Remaining = Dt;
					while(Remaining > 0.0f)
					{
						const float Step = minimum(Remaining, 0.008f);
						Remaining -= Step;
						s_Brick.m_BallPos += s_Brick.m_BallVel * Step;

						if(s_Brick.m_BallPos.x < BallRadius)
						{
							s_Brick.m_BallPos.x = BallRadius;
							s_Brick.m_BallVel.x = fabs(s_Brick.m_BallVel.x);
						}
						else if(s_Brick.m_BallPos.x > 1.0f - BallRadius)
						{
							s_Brick.m_BallPos.x = 1.0f - BallRadius;
							s_Brick.m_BallVel.x = -fabs(s_Brick.m_BallVel.x);
						}

						if(s_Brick.m_BallPos.y < BallRadius)
						{
							s_Brick.m_BallPos.y = BallRadius;
							s_Brick.m_BallVel.y = fabs(s_Brick.m_BallVel.y);
						}

						const float PaddleY = 0.90f;
						const float PaddleHalfW = s_Brick.m_PaddleW * 0.5f;
						if(s_Brick.m_BallVel.y > 0.0f && s_Brick.m_BallPos.y + BallRadius >= PaddleY && s_Brick.m_BallPos.y + BallRadius <= PaddleY + 0.03f && fabs(s_Brick.m_BallPos.x - s_Brick.m_PaddleX) <= PaddleHalfW + BallRadius)
						{
							s_Brick.m_BallPos.y = PaddleY - BallRadius;
							const float Hit = (s_Brick.m_BallPos.x - s_Brick.m_PaddleX) / maximum(0.01f, PaddleHalfW);
							s_Brick.m_BallVel.y = -fabs(s_Brick.m_BallVel.y) * 1.01f;
							s_Brick.m_BallVel.x = std::clamp(s_Brick.m_BallVel.x + Hit * 0.25f, -1.2f, 1.2f);
						}

						const float BricksX = 0.06f;
						const float BricksY = 0.08f;
						const float BricksH = 0.34f;
						const float CellW = (1.0f - BricksX * 2.0f) / s_Brick.m_Cols;
						const float CellH = BricksH / s_Brick.m_Rows;
						if(s_Brick.m_BallPos.x >= BricksX && s_Brick.m_BallPos.x < 1.0f - BricksX && s_Brick.m_BallPos.y >= BricksY && s_Brick.m_BallPos.y < BricksY + BricksH)
						{
							const int BX = std::clamp((int)((s_Brick.m_BallPos.x - BricksX) / CellW), 0, s_Brick.m_Cols - 1);
							const int BY = std::clamp((int)((s_Brick.m_BallPos.y - BricksY) / CellH), 0, s_Brick.m_Rows - 1);
							const int BIdx = BrickIdx(BX, BY);
							if(s_Brick.m_vBricks[BIdx])
							{
								s_Brick.m_vBricks[BIdx] = 0;
								s_Brick.m_RemainingBricks--;
								s_Brick.m_Score += 10;
								s_Brick.m_BestScore = maximum(s_Brick.m_BestScore, s_Brick.m_Score);

								const float CellCX = BricksX + (BX + 0.5f) * CellW;
								const float CellCY = BricksY + (BY + 0.5f) * CellH;
								const float NX = (s_Brick.m_BallPos.x - CellCX) / maximum(0.001f, CellW);
								const float NY = (s_Brick.m_BallPos.y - CellCY) / maximum(0.001f, CellH);
								if(fabs(NX) > fabs(NY))
									s_Brick.m_BallVel.x *= -1.0f;
								else
									s_Brick.m_BallVel.y *= -1.0f;

								if(s_Brick.m_RemainingBricks <= 0)
								{
									s_Brick.m_Won = true;
									break;
								}
							}
						}

						if(s_Brick.m_BallPos.y > 1.0f + BallRadius)
						{
							s_Brick.m_Lives--;
							if(s_Brick.m_Lives <= 0)
								s_Brick.m_GameOver = true;
							else
								ResetBrickRound();
							break;
						}
					}
				}
			}

			const float BricksX = 0.06f;
			const float BricksY = 0.08f;
			const float BricksH = 0.34f;
			const float CellW = (1.0f - BricksX * 2.0f) / s_Brick.m_Cols;
			const float CellH = BricksH / s_Brick.m_Rows;
			for(int y = 0; y < s_Brick.m_Rows; ++y)
				for(int x = 0; x < s_Brick.m_Cols; ++x)
				{
					if(!s_Brick.m_vBricks[BrickIdx(x, y)])
						continue;
					CUIRect Brick;
					Brick.x = Arena.x + Arena.w * (BricksX + x * CellW);
					Brick.y = Arena.y + Arena.h * (BricksY + y * CellH);
					Brick.w = Arena.w * CellW;
					Brick.h = Arena.h * CellH;
					const float PadX = maximum(1.0f, Brick.w * 0.06f);
					const float PadY = maximum(1.0f, Brick.h * 0.12f);
					Brick.x += PadX;
					Brick.y += PadY;
					Brick.w -= PadX * 2.0f;
					Brick.h -= PadY * 2.0f;
					const float T = y / (float)maximum(1, s_Brick.m_Rows - 1);
					const ColorRGBA Col = BlendColors(ColorRGBA(0.35f, 0.83f, 1.0f, 0.95f), ColorRGBA(1.0f, 0.52f, 0.43f, 0.95f), T);
					Brick.Draw(Col, IGraphics::CORNER_ALL, 3.0f);
				}

			CUIRect Paddle;
			Paddle.w = Arena.w * s_Brick.m_PaddleW;
			Paddle.h = Arena.h * 0.025f;
			Paddle.x = Arena.x + Arena.w * s_Brick.m_PaddleX - Paddle.w * 0.5f;
			Paddle.y = Arena.y + Arena.h * 0.90f;
			Paddle.Draw(ColorRGBA(0.86f, 0.92f, 1.0f, 0.95f), IGraphics::CORNER_ALL, 3.0f);

			const float BallRadiusPx = Arena.h * 0.017f;
			CUIRect Ball;
			Ball.w = BallRadiusPx * 2.0f;
			Ball.h = BallRadiusPx * 2.0f;
			Ball.x = Arena.x + Arena.w * s_Brick.m_BallPos.x - Ball.w * 0.5f;
			Ball.y = Arena.y + Arena.h * s_Brick.m_BallPos.y - Ball.h * 0.5f;
			Ball.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.98f), IGraphics::CORNER_ALL, Ball.h * 0.5f);

			if(s_Brick.m_GameOver || s_Brick.m_Won)
			{
				CUIRect Overlay = Arena;
				Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.40f), IGraphics::CORNER_ALL, 6.0f);
				Ui()->DoLabel(&Overlay, s_Brick.m_Won ? TCLocalize("All bricks cleared") : TCLocalize("Game Over"), HEADLINE_FONT_SIZE, TEXTALIGN_MC);
			}
	}
	else if(s_SelectedGame == FUN_GAME_FOUR_IN_A_ROW)
	{
		struct SConnect4State
		{
			int m_Mode = 1; // 0 local, 1 bot
			bool m_Initialized = false;
			int m_W = 7;
			int m_H = 6;
			std::vector<int> m_vBoard;
			int m_Turn = 1;
			int m_Moves = 0;
			bool m_GameOver = false;
			int m_Winner = 0; // 0 none, 1/2 player, 3 draw
			std::array<int, 4> m_aWinCells = {-1, -1, -1, -1};
		};

		static SConnect4State s_Connect4;

		auto C4Idx = [&](int X, int Y) {
			return Y * s_Connect4.m_W + X;
		};
		auto ResetConnect4 = [&]() {
			s_Connect4.m_Initialized = true;
			s_Connect4.m_vBoard.assign((size_t)s_Connect4.m_W * s_Connect4.m_H, 0);
			s_Connect4.m_Turn = 1;
			s_Connect4.m_Moves = 0;
			s_Connect4.m_GameOver = false;
			s_Connect4.m_Winner = 0;
			s_Connect4.m_aWinCells = {-1, -1, -1, -1};
		};
		auto CanDropC4 = [&](int Col) {
			return Col >= 0 && Col < s_Connect4.m_W && s_Connect4.m_vBoard[C4Idx(Col, 0)] == 0;
		};
		auto DropC4 = [&](int Col, int Piece) {
			for(int y = s_Connect4.m_H - 1; y >= 0; --y)
			{
				const int Idx = C4Idx(Col, y);
				if(s_Connect4.m_vBoard[Idx] == 0)
				{
					s_Connect4.m_vBoard[Idx] = Piece;
					return y;
				}
			}
			return -1;
		};
		auto CheckFourFrom = [&](int X, int Y, int Piece, bool StoreLine) {
			const ivec2 aDirs[4] = {ivec2(1, 0), ivec2(0, 1), ivec2(1, 1), ivec2(1, -1)};
			for(const ivec2 &Dir : aDirs)
			{
				int Neg = 0;
				int Pos = 0;
				while(X - (Neg + 1) * Dir.x >= 0 && X - (Neg + 1) * Dir.x < s_Connect4.m_W && Y - (Neg + 1) * Dir.y >= 0 && Y - (Neg + 1) * Dir.y < s_Connect4.m_H && s_Connect4.m_vBoard[C4Idx(X - (Neg + 1) * Dir.x, Y - (Neg + 1) * Dir.y)] == Piece)
					Neg++;
				while(X + (Pos + 1) * Dir.x >= 0 && X + (Pos + 1) * Dir.x < s_Connect4.m_W && Y + (Pos + 1) * Dir.y >= 0 && Y + (Pos + 1) * Dir.y < s_Connect4.m_H && s_Connect4.m_vBoard[C4Idx(X + (Pos + 1) * Dir.x, Y + (Pos + 1) * Dir.y)] == Piece)
					Pos++;
				if(1 + Neg + Pos >= 4)
				{
					if(StoreLine)
					{
						const int StartX = X - Neg * Dir.x;
						const int StartY = Y - Neg * Dir.y;
						for(int i = 0; i < 4; ++i)
							s_Connect4.m_aWinCells[i] = C4Idx(StartX + i * Dir.x, StartY + i * Dir.y);
					}
					return true;
				}
			}
			return false;
		};
		auto PickBotCol = [&]() {
			auto TestPiece = [&](int Piece) {
				for(int Col = 0; Col < s_Connect4.m_W; ++Col)
				{
					if(!CanDropC4(Col))
						continue;
					const int Row = DropC4(Col, Piece);
					const bool Win = Row >= 0 && CheckFourFrom(Col, Row, Piece, false);
					s_Connect4.m_vBoard[C4Idx(Col, Row)] = 0;
					if(Win)
						return Col;
				}
				return -1;
			};

			int Col = TestPiece(2);
			if(Col >= 0)
				return Col;
			Col = TestPiece(1);
			if(Col >= 0)
				return Col;

			const int aOrder[7] = {3, 2, 4, 1, 5, 0, 6};
			for(int i = 0; i < 7; ++i)
				if(CanDropC4(aOrder[i]))
					return aOrder[i];
			return -1;
		};

			if(!s_Connect4.m_Initialized)
				ResetConnect4();

			CUIRect TopBar, BoardArea;
			GameContent.HSplitTop(LINE_SIZE * 1.2f, &TopBar, &BoardArea);
			BoardArea.HSplitTop(MARGIN_SMALL, nullptr, &BoardArea);

			CUIRect Status, BtnArea, RestartButton;
			TopBar.VSplitLeft(360.0f, &Status, &BtnArea);
			BtnArea.VSplitRight(110.0f, &BtnArea, &RestartButton);

			const char *pStatus = s_Connect4.m_GameOver ? (s_Connect4.m_Winner == 1 ? TCLocalize("Red wins") : s_Connect4.m_Winner == 2 ? TCLocalize("Yellow wins") :
																		      TCLocalize("Draw")) :
								      (s_Connect4.m_Turn == 1 ? TCLocalize("Turn: Red") : TCLocalize("Turn: Yellow"));
			Ui()->DoLabel(&Status, pStatus, FONT_SIZE, TEXTALIGN_ML);
			static CButtonContainer s_Connect4RestartButton;
			if(DoButton_Menu(&s_Connect4RestartButton, TCLocalize("Restart"), 0, &RestartButton))
				ResetConnect4();

			CUIRect Board;
			const float BoardW = minimum(BoardArea.w, BoardArea.h * (7.0f / 6.0f));
			Board.w = BoardW;
			Board.h = BoardW * 6.0f / 7.0f;
			Board.x = BoardArea.x + (BoardArea.w - Board.w) * 0.5f;
			Board.y = BoardArea.y + (BoardArea.h - Board.h) * 0.5f;
			Board.Draw(ColorRGBA(0.12f, 0.24f, 0.62f, 0.95f), IGraphics::CORNER_ALL, 8.0f);

			const float CellW = Board.w / s_Connect4.m_W;
			const float CellH = Board.h / s_Connect4.m_H;
			int HoverCol = -1;
			if(Ui()->MouseInside(&Board))
			{
				const vec2 Mouse = Ui()->MousePos();
				HoverCol = std::clamp((int)((Mouse.x - Board.x) / CellW), 0, s_Connect4.m_W - 1);
			}

			auto CommitMove = [&](int Col) {
				if(!CanDropC4(Col))
					return;
				const int Piece = s_Connect4.m_Turn;
				const int Row = DropC4(Col, Piece);
				if(Row < 0)
					return;
				s_Connect4.m_Moves++;
				if(CheckFourFrom(Col, Row, Piece, true))
				{
					s_Connect4.m_GameOver = true;
					s_Connect4.m_Winner = Piece;
				}
				else if(s_Connect4.m_Moves >= s_Connect4.m_W * s_Connect4.m_H)
				{
					s_Connect4.m_GameOver = true;
					s_Connect4.m_Winner = 3;
				}
				else
				{
					s_Connect4.m_Turn = s_Connect4.m_Turn == 1 ? 2 : 1;
				}
			};

			if(!s_Connect4.m_GameOver)
			{
				const bool BotTurn = s_Connect4.m_Mode == 1 && s_Connect4.m_Turn == 2;
				if(BotTurn)
				{
					const int Col = PickBotCol();
					if(Col >= 0)
						CommitMove(Col);
				}
				else if(HoverCol >= 0 && Ui()->MouseButtonClicked(0))
				{
					CommitMove(HoverCol);
				}
			}

			for(int y = 0; y < s_Connect4.m_H; ++y)
			{
				for(int x = 0; x < s_Connect4.m_W; ++x)
				{
					CUIRect Cell;
					Cell.x = Board.x + x * CellW;
					Cell.y = Board.y + y * CellH;
					Cell.w = CellW;
					Cell.h = CellH;
					Cell.Draw(ColorRGBA(0.10f, 0.20f, 0.55f, 0.96f), IGraphics::CORNER_NONE, 0.0f);

					CUIRect Piece = Cell;
					Piece.Margin(minimum(Cell.w, Cell.h) * 0.12f, &Piece);
					int CellValue = s_Connect4.m_vBoard[C4Idx(x, y)];
					ColorRGBA Col(0.08f, 0.12f, 0.27f, 0.95f);
					if(CellValue == 1)
						Col = ColorRGBA(0.95f, 0.33f, 0.30f, 0.98f);
					else if(CellValue == 2)
						Col = ColorRGBA(1.0f, 0.85f, 0.32f, 0.98f);
					Piece.Draw(Col, IGraphics::CORNER_ALL, Piece.h * 0.5f);

					bool IsWinning = false;
					for(int WinIdx : s_Connect4.m_aWinCells)
						if(WinIdx == C4Idx(x, y))
							IsWinning = true;
					if(IsWinning)
					{
						CUIRect Glow = Piece;
						Glow.Margin(Piece.w * 0.10f, &Glow);
						Glow.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.22f), IGraphics::CORNER_ALL, Glow.h * 0.5f);
					}
				}
			}

			if(!s_Connect4.m_GameOver && HoverCol >= 0 && (s_Connect4.m_Mode == 0 || s_Connect4.m_Turn == 1))
			{
				for(int y = s_Connect4.m_H - 1; y >= 0; --y)
				{
					if(s_Connect4.m_vBoard[C4Idx(HoverCol, y)] != 0)
						continue;
					CUIRect Hint;
					Hint.x = Board.x + HoverCol * CellW + CellW * 0.18f;
					Hint.y = Board.y + y * CellH + CellH * 0.18f;
					Hint.w = CellW * 0.64f;
					Hint.h = CellH * 0.64f;
					const ColorRGBA HintCol = s_Connect4.m_Turn == 1 ? ColorRGBA(0.95f, 0.33f, 0.30f, 0.34f) : ColorRGBA(1.0f, 0.85f, 0.32f, 0.34f);
					Hint.Draw(HintCol, IGraphics::CORNER_ALL, Hint.h * 0.5f);
					break;
				}
			}
	}
	else if(s_SelectedGame == FUN_GAME_MINI_GOLF)
	{
		struct SGolfObstacle
		{
			float m_X = 0.0f;
			float m_Y = 0.0f;
			float m_W = 0.0f;
			float m_H = 0.0f;
		};

		struct SMiniGolfState
		{
			int m_StartLevelPreset = 0;
			bool m_Initialized = false;
			int m_CurrentLevel = 0;
			int m_TotalLevels = 3;
			vec2 m_BallPos = vec2(0.14f, 0.5f);
			vec2 m_BallVel = vec2(0.0f, 0.0f);
			vec2 m_StartPos = vec2(0.14f, 0.5f);
			vec2 m_HolePos = vec2(0.88f, 0.5f);
			std::vector<SGolfObstacle> m_vObstacles;
			int m_LevelStrokes = 0;
			int m_TotalStrokes = 0;
			int m_BestTotal = 0;
			bool m_CourseDone = false;
			float m_AimAngle = -0.30f;
			float m_AimPower = 0.55f;
			int64_t m_LastTick = 0;
			int64_t m_LevelAdvanceAt = 0;
		};

		static SMiniGolfState s_Golf;

		auto IsBallMoving = [&]() {
			return fabs(s_Golf.m_BallVel.x) + fabs(s_Golf.m_BallVel.y) > 0.001f;
		};
		auto LoadGolfLevel = [&](int Level) {
			s_Golf.m_vObstacles.clear();
			auto AddObs = [&](float X, float Y, float W, float H) {
				SGolfObstacle Obs;
				Obs.m_X = X;
				Obs.m_Y = Y;
				Obs.m_W = W;
				Obs.m_H = H;
				s_Golf.m_vObstacles.push_back(Obs);
			};

			switch(Level)
			{
			case 0:
				s_Golf.m_StartPos = vec2(0.12f, 0.75f);
				s_Golf.m_HolePos = vec2(0.88f, 0.24f);
				AddObs(0.34f, 0.52f, 0.10f, 0.34f);
				AddObs(0.56f, 0.14f, 0.10f, 0.34f);
				break;
			case 1:
				s_Golf.m_StartPos = vec2(0.12f, 0.22f);
				s_Golf.m_HolePos = vec2(0.88f, 0.78f);
				AddObs(0.28f, 0.12f, 0.08f, 0.56f);
				AddObs(0.48f, 0.34f, 0.20f, 0.08f);
				AddObs(0.72f, 0.46f, 0.08f, 0.42f);
				break;
			default:
				s_Golf.m_StartPos = vec2(0.16f, 0.50f);
				s_Golf.m_HolePos = vec2(0.88f, 0.50f);
				AddObs(0.28f, 0.16f, 0.08f, 0.44f);
				AddObs(0.44f, 0.40f, 0.08f, 0.44f);
				AddObs(0.60f, 0.16f, 0.08f, 0.44f);
				AddObs(0.76f, 0.40f, 0.08f, 0.44f);
				break;
			}
		};
		auto StartGolfLevel = [&]() {
			LoadGolfLevel(s_Golf.m_CurrentLevel);
			s_Golf.m_BallPos = s_Golf.m_StartPos;
			s_Golf.m_BallVel = vec2(0.0f, 0.0f);
			s_Golf.m_LevelStrokes = 0;
			s_Golf.m_LevelAdvanceAt = 0;
			s_Golf.m_AimAngle = atan2f(s_Golf.m_HolePos.y - s_Golf.m_BallPos.y, s_Golf.m_HolePos.x - s_Golf.m_BallPos.x);
		};
		auto ResetMiniGolf = [&]() {
			s_Golf.m_Initialized = true;
			s_Golf.m_CurrentLevel = s_Golf.m_StartLevelPreset;
			s_Golf.m_TotalStrokes = 0;
			s_Golf.m_CourseDone = false;
			s_Golf.m_AimPower = 0.55f;
			s_Golf.m_LastTick = time_get();
			StartGolfLevel();
		};

			if(!s_Golf.m_Initialized)
				ResetMiniGolf();

			CUIRect TopBar, ArenaArea;
			GameContent.HSplitTop(LINE_SIZE * 1.2f, &TopBar, &ArenaArea);
			ArenaArea.HSplitTop(MARGIN_SMALL, nullptr, &ArenaArea);

			CUIRect Stats, BtnArea, RestartButton;
			TopBar.VSplitLeft(470.0f, &Stats, &BtnArea);
			BtnArea.VSplitRight(110.0f, &BtnArea, &RestartButton);

			char aBest[32];
			if(s_Golf.m_BestTotal > 0)
				str_format(aBest, sizeof(aBest), "%d", s_Golf.m_BestTotal);
			else
				str_copy(aBest, "-", sizeof(aBest));
			char aStats[196];
			str_format(aStats, sizeof(aStats), "Level: %d/%d   Level strokes: %d   Total: %d   Best: %s", s_Golf.m_CurrentLevel + 1, s_Golf.m_TotalLevels, s_Golf.m_LevelStrokes, s_Golf.m_TotalStrokes, aBest);
			Ui()->DoLabel(&Stats, aStats, FONT_SIZE, TEXTALIGN_ML);
			static CButtonContainer s_GolfRestartButton;
			if(DoButton_Menu(&s_GolfRestartButton, TCLocalize("Restart"), 0, &RestartButton))
				ResetMiniGolf();

			CUIRect Arena = ArenaArea;
			const float PreferredW = minimum(ArenaArea.w, ArenaArea.h * 1.9f);
			Arena.w = PreferredW;
			Arena.h = PreferredW / 1.9f;
			Arena.x = ArenaArea.x + (ArenaArea.w - Arena.w) * 0.5f;
			Arena.y = ArenaArea.y + (ArenaArea.h - Arena.h) * 0.5f;
			Arena.Draw(ColorRGBA(0.12f, 0.45f, 0.20f, 0.95f), IGraphics::CORNER_ALL, 10.0f);
			Arena.DrawOutline(ColorRGBA(0.22f, 0.64f, 0.32f, 0.45f));

			const int64_t Now = time_get();
			if(!s_Golf.m_CourseDone)
			{
				if(s_Golf.m_LevelAdvanceAt != 0)
				{
					if(Now >= s_Golf.m_LevelAdvanceAt)
					{
						s_Golf.m_LevelAdvanceAt = 0;
						s_Golf.m_CurrentLevel++;
						if(s_Golf.m_CurrentLevel >= s_Golf.m_TotalLevels)
						{
							s_Golf.m_CourseDone = true;
							if(s_Golf.m_BestTotal == 0 || s_Golf.m_TotalStrokes < s_Golf.m_BestTotal)
								s_Golf.m_BestTotal = s_Golf.m_TotalStrokes;
						}
						else
						{
							StartGolfLevel();
						}
					}
				}
				else
				{
					float Dt = (Now - s_Golf.m_LastTick) / (float)time_freq();
					s_Golf.m_LastTick = Now;
					Dt = std::clamp(Dt, 0.0f, 0.05f);

					if(!IsBallMoving())
					{
						if(Input()->KeyIsPressed(KEY_LEFT) || Input()->KeyIsPressed(KEY_A))
							s_Golf.m_AimAngle -= Dt * 2.1f;
						if(Input()->KeyIsPressed(KEY_RIGHT) || Input()->KeyIsPressed(KEY_D))
							s_Golf.m_AimAngle += Dt * 2.1f;
						if(Input()->KeyIsPressed(KEY_UP) || Input()->KeyIsPressed(KEY_W))
							s_Golf.m_AimPower = minimum(1.0f, s_Golf.m_AimPower + Dt * 0.9f);
						if(Input()->KeyIsPressed(KEY_DOWN) || Input()->KeyIsPressed(KEY_S))
							s_Golf.m_AimPower = maximum(0.15f, s_Golf.m_AimPower - Dt * 0.9f);

						if(Ui()->MouseInside(&Arena))
						{
							const vec2 Mouse = Ui()->MousePos();
							const vec2 BallPx(Arena.x + s_Golf.m_BallPos.x * Arena.w, Arena.y + s_Golf.m_BallPos.y * Arena.h);
							s_Golf.m_AimAngle = atan2f(Mouse.y - BallPx.y, Mouse.x - BallPx.x);
						}

						const bool Shoot = Input()->KeyPress(KEY_SPACE) || (Ui()->MouseInside(&Arena) && Ui()->MouseButtonClicked(0));
						if(Shoot)
						{
							const float Force = 0.45f + s_Golf.m_AimPower * 1.1f;
							s_Golf.m_BallVel = vec2(cosf(s_Golf.m_AimAngle) * Force, sinf(s_Golf.m_AimAngle) * Force);
							s_Golf.m_LevelStrokes++;
							s_Golf.m_TotalStrokes++;
						}
					}
					else
					{
						float Remaining = Dt;
						const float BallRadius = 0.018f;
						const float HoleRadius = 0.026f;
						while(Remaining > 0.0f)
						{
							const float Step = minimum(0.008f, Remaining);
							Remaining -= Step;
							s_Golf.m_BallPos += s_Golf.m_BallVel * Step;

							if(s_Golf.m_BallPos.x < BallRadius)
							{
								s_Golf.m_BallPos.x = BallRadius;
								s_Golf.m_BallVel.x = fabs(s_Golf.m_BallVel.x) * 0.92f;
							}
							else if(s_Golf.m_BallPos.x > 1.0f - BallRadius)
							{
								s_Golf.m_BallPos.x = 1.0f - BallRadius;
								s_Golf.m_BallVel.x = -fabs(s_Golf.m_BallVel.x) * 0.92f;
							}
							if(s_Golf.m_BallPos.y < BallRadius)
							{
								s_Golf.m_BallPos.y = BallRadius;
								s_Golf.m_BallVel.y = fabs(s_Golf.m_BallVel.y) * 0.92f;
							}
							else if(s_Golf.m_BallPos.y > 1.0f - BallRadius)
							{
								s_Golf.m_BallPos.y = 1.0f - BallRadius;
								s_Golf.m_BallVel.y = -fabs(s_Golf.m_BallVel.y) * 0.92f;
							}

							for(const SGolfObstacle &Obs : s_Golf.m_vObstacles)
							{
								const float Left = Obs.m_X - BallRadius;
								const float Right = Obs.m_X + Obs.m_W + BallRadius;
								const float Top = Obs.m_Y - BallRadius;
								const float Bottom = Obs.m_Y + Obs.m_H + BallRadius;
								if(s_Golf.m_BallPos.x <= Left || s_Golf.m_BallPos.x >= Right || s_Golf.m_BallPos.y <= Top || s_Golf.m_BallPos.y >= Bottom)
									continue;

								const float dL = fabs(s_Golf.m_BallPos.x - Left);
								const float dR = fabs(Right - s_Golf.m_BallPos.x);
								const float dT = fabs(s_Golf.m_BallPos.y - Top);
								const float dB = fabs(Bottom - s_Golf.m_BallPos.y);
								const float MinD = minimum(minimum(dL, dR), minimum(dT, dB));
								if(MinD == dL)
								{
									s_Golf.m_BallPos.x = Left;
									s_Golf.m_BallVel.x = -fabs(s_Golf.m_BallVel.x) * 0.88f;
								}
								else if(MinD == dR)
								{
									s_Golf.m_BallPos.x = Right;
									s_Golf.m_BallVel.x = fabs(s_Golf.m_BallVel.x) * 0.88f;
								}
								else if(MinD == dT)
								{
									s_Golf.m_BallPos.y = Top;
									s_Golf.m_BallVel.y = -fabs(s_Golf.m_BallVel.y) * 0.88f;
								}
								else
								{
									s_Golf.m_BallPos.y = Bottom;
									s_Golf.m_BallVel.y = fabs(s_Golf.m_BallVel.y) * 0.88f;
								}
							}

							const float Friction = maximum(0.0f, 1.0f - Step * 1.15f);
							s_Golf.m_BallVel *= Friction;
							const float SpeedSq = s_Golf.m_BallVel.x * s_Golf.m_BallVel.x + s_Golf.m_BallVel.y * s_Golf.m_BallVel.y;
							if(SpeedSq < 0.002f)
								s_Golf.m_BallVel = vec2(0.0f, 0.0f);

							const float DX = s_Golf.m_BallPos.x - s_Golf.m_HolePos.x;
							const float DY = s_Golf.m_BallPos.y - s_Golf.m_HolePos.y;
							const float DistSq = DX * DX + DY * DY;
							if(DistSq <= HoleRadius * HoleRadius && SpeedSq <= 0.09f)
							{
								s_Golf.m_BallPos = s_Golf.m_HolePos;
								s_Golf.m_BallVel = vec2(0.0f, 0.0f);
								s_Golf.m_LevelAdvanceAt = Now + (int64_t)(time_freq() * 0.6f);
								break;
							}
						}
					}
				}
			}
			else
			{
				s_Golf.m_LastTick = Now;
			}

			for(const SGolfObstacle &Obs : s_Golf.m_vObstacles)
			{
				CUIRect ObRect;
				ObRect.x = Arena.x + Arena.w * Obs.m_X;
				ObRect.y = Arena.y + Arena.h * Obs.m_Y;
				ObRect.w = Arena.w * Obs.m_W;
				ObRect.h = Arena.h * Obs.m_H;
				ObRect.Draw(ColorRGBA(0.20f, 0.34f, 0.18f, 0.95f), IGraphics::CORNER_ALL, 4.0f);
			}

			CUIRect Hole;
			Hole.w = Arena.h * 0.06f;
			Hole.h = Arena.h * 0.06f;
			Hole.x = Arena.x + Arena.w * s_Golf.m_HolePos.x - Hole.w * 0.5f;
			Hole.y = Arena.y + Arena.h * s_Golf.m_HolePos.y - Hole.h * 0.5f;
			Hole.Draw(ColorRGBA(0.05f, 0.08f, 0.04f, 0.98f), IGraphics::CORNER_ALL, Hole.h * 0.5f);

			CUIRect Ball;
			Ball.w = Arena.h * 0.05f;
			Ball.h = Arena.h * 0.05f;
			Ball.x = Arena.x + Arena.w * s_Golf.m_BallPos.x - Ball.w * 0.5f;
			Ball.y = Arena.y + Arena.h * s_Golf.m_BallPos.y - Ball.h * 0.5f;
			Ball.Draw(ColorRGBA(0.98f, 0.99f, 1.0f, 0.98f), IGraphics::CORNER_ALL, Ball.h * 0.5f);

			if(!s_Golf.m_CourseDone && s_Golf.m_LevelAdvanceAt == 0 && !IsBallMoving())
			{
				const vec2 Start(Arena.x + Arena.w * s_Golf.m_BallPos.x, Arena.y + Arena.h * s_Golf.m_BallPos.y);
				const vec2 End = Start + vec2(cosf(s_Golf.m_AimAngle), sinf(s_Golf.m_AimAngle)) * (Arena.h * (0.14f + s_Golf.m_AimPower * 0.22f));
				Graphics()->TextureClear();
				Graphics()->LinesBegin();
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.35f);
				IGraphics::CLineItem Line(Start.x, Start.y, End.x, End.y);
				Graphics()->LinesDraw(&Line, 1);
				Graphics()->LinesEnd();
			}

			if(s_Golf.m_LevelAdvanceAt != 0)
			{
				CUIRect Overlay = Arena;
				Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.24f), IGraphics::CORNER_ALL, 8.0f);
				Ui()->DoLabel(&Overlay, TCLocalize("Hole complete"), HEADLINE_FONT_SIZE, TEXTALIGN_MC);
			}
			if(s_Golf.m_CourseDone)
			{
				CUIRect Overlay = Arena;
				Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.34f), IGraphics::CORNER_ALL, 8.0f);
				Ui()->DoLabel(&Overlay, TCLocalize("Course complete"), HEADLINE_FONT_SIZE, TEXTALIGN_MC);
			}
	}
	else if(s_SelectedGame == FUN_GAME_CHECKERS)
	{
		struct SCheckerMove
		{
			int m_From = -1;
			int m_To = -1;
			int m_Capture = -1;
		};

		struct SCheckersState
		{
			int m_Mode = 1; // 0 local, 1 bot
			bool m_Initialized = false;
			std::array<int, 64> m_aBoard{};
			int m_Turn = 1; // 1 white, -1 black
			int m_Selected = -1;
			int m_MoveCount = 0;
			bool m_GameOver = false;
			int m_Winner = 0;
		};

		static SCheckersState s_Checkers;

		auto ChIdx = [&](int X, int Y) {
			return Y * 8 + X;
		};
		auto ChInside = [&](int X, int Y) {
			return X >= 0 && Y >= 0 && X < 8 && Y < 8;
		};
		auto ChDark = [&](int X, int Y) {
			return ((X + Y) & 1) != 0;
		};
		auto ResetCheckers = [&]() {
			s_Checkers.m_Initialized = true;
			s_Checkers.m_aBoard.fill(0);
			for(int y = 0; y < 3; ++y)
				for(int x = 0; x < 8; ++x)
					if(ChDark(x, y))
						s_Checkers.m_aBoard[ChIdx(x, y)] = -1;
			for(int y = 5; y < 8; ++y)
				for(int x = 0; x < 8; ++x)
					if(ChDark(x, y))
						s_Checkers.m_aBoard[ChIdx(x, y)] = 1;
			s_Checkers.m_Turn = 1;
			s_Checkers.m_Selected = -1;
			s_Checkers.m_MoveCount = 0;
			s_Checkers.m_GameOver = false;
			s_Checkers.m_Winner = 0;
		};
		auto CollectMoves = [&](int TurnSign) {
			std::vector<SCheckerMove> vCaptures;
			std::vector<SCheckerMove> vSimple;

			for(int Pos = 0; Pos < 64; ++Pos)
			{
				const int Piece = s_Checkers.m_aBoard[Pos];
				if(Piece == 0 || Piece * TurnSign <= 0)
					continue;

				const int X = Pos % 8;
				const int Y = Pos / 8;
				const bool King = abs(Piece) == 2;
				const int Forward = Piece > 0 ? -1 : 1;

				auto TrySimple = [&](int Dx, int Dy) {
					const int NX = X + Dx;
					const int NY = Y + Dy;
					if(!ChInside(NX, NY))
						return;
					const int To = ChIdx(NX, NY);
					if(s_Checkers.m_aBoard[To] == 0)
						vSimple.push_back({Pos, To, -1});
				};
				auto TryCapture = [&](int Dx, int Dy) {
					const int MX = X + Dx;
					const int MY = Y + Dy;
					const int TX = X + Dx * 2;
					const int TY = Y + Dy * 2;
					if(!ChInside(MX, MY) || !ChInside(TX, TY))
						return;
					const int Mid = ChIdx(MX, MY);
					const int To = ChIdx(TX, TY);
					if(s_Checkers.m_aBoard[Mid] == 0 || s_Checkers.m_aBoard[Mid] * Piece >= 0 || s_Checkers.m_aBoard[To] != 0)
						return;
					vCaptures.push_back({Pos, To, Mid});
				};

				if(King)
				{
					for(int Dy : {-1, 1})
						for(int Dx : {-1, 1})
						{
							TrySimple(Dx, Dy);
							TryCapture(Dx, Dy);
						}
				}
				else
				{
					TrySimple(-1, Forward);
					TrySimple(1, Forward);
					for(int Dy : {-1, 1})
						for(int Dx : {-1, 1})
							TryCapture(Dx, Dy);
				}
			}
			if(!vCaptures.empty())
				return vCaptures;
			return vSimple;
		};
		auto ApplyMove = [&](const SCheckerMove &Move) {
			int Piece = s_Checkers.m_aBoard[Move.m_From];
			s_Checkers.m_aBoard[Move.m_From] = 0;
			s_Checkers.m_aBoard[Move.m_To] = Piece;
			if(Move.m_Capture >= 0)
				s_Checkers.m_aBoard[Move.m_Capture] = 0;

			const int ToY = Move.m_To / 8;
			if(Piece == 1 && ToY == 0)
				s_Checkers.m_aBoard[Move.m_To] = 2;
			else if(Piece == -1 && ToY == 7)
				s_Checkers.m_aBoard[Move.m_To] = -2;

			s_Checkers.m_MoveCount++;
			s_Checkers.m_Selected = -1;
			s_Checkers.m_Turn = -s_Checkers.m_Turn;
		};
		auto CheckEnd = [&]() {
			int WhiteCount = 0;
			int BlackCount = 0;
			for(int Piece : s_Checkers.m_aBoard)
			{
				if(Piece > 0)
					WhiteCount++;
				else if(Piece < 0)
					BlackCount++;
			}
			if(WhiteCount == 0)
			{
				s_Checkers.m_GameOver = true;
				s_Checkers.m_Winner = -1;
				return;
			}
			if(BlackCount == 0)
			{
				s_Checkers.m_GameOver = true;
				s_Checkers.m_Winner = 1;
				return;
			}
			const std::vector<SCheckerMove> vMoves = CollectMoves(s_Checkers.m_Turn);
			if(vMoves.empty())
			{
				s_Checkers.m_GameOver = true;
				s_Checkers.m_Winner = -s_Checkers.m_Turn;
			}
		};

			if(!s_Checkers.m_Initialized)
				ResetCheckers();

			CUIRect TopBar, BoardArea;
			GameContent.HSplitTop(LINE_SIZE * 1.2f, &TopBar, &BoardArea);
			BoardArea.HSplitTop(MARGIN_SMALL, nullptr, &BoardArea);

			CUIRect Status, BtnArea, RestartButton;
			TopBar.VSplitLeft(380.0f, &Status, &BtnArea);
			BtnArea.VSplitRight(110.0f, &BtnArea, &RestartButton);

			const char *pStatus = s_Checkers.m_GameOver ? (s_Checkers.m_Winner > 0 ? TCLocalize("White wins") : TCLocalize("Black wins")) : (s_Checkers.m_Turn > 0 ? TCLocalize("Turn: White") : TCLocalize("Turn: Black"));
			Ui()->DoLabel(&Status, pStatus, FONT_SIZE, TEXTALIGN_ML);
			static CButtonContainer s_CheckersRestartButton;
			if(DoButton_Menu(&s_CheckersRestartButton, TCLocalize("Restart"), 0, &RestartButton))
				ResetCheckers();

			CUIRect Board;
			const float BoardSize = minimum(BoardArea.w, BoardArea.h);
			Board.w = BoardSize;
			Board.h = BoardSize;
			Board.x = BoardArea.x + (BoardArea.w - Board.w) * 0.5f;
			Board.y = BoardArea.y + (BoardArea.h - Board.h) * 0.5f;
			Board.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.20f), IGraphics::CORNER_ALL, 5.0f);

			std::vector<SCheckerMove> vLegalMoves = s_Checkers.m_GameOver ? std::vector<SCheckerMove>() : CollectMoves(s_Checkers.m_Turn);
			std::vector<SCheckerMove> vSelectedMoves;
			for(const SCheckerMove &Move : vLegalMoves)
				if(Move.m_From == s_Checkers.m_Selected)
					vSelectedMoves.push_back(Move);

			if(!s_Checkers.m_GameOver)
			{
				const bool BotTurn = s_Checkers.m_Mode == 1 && s_Checkers.m_Turn < 0;
				if(BotTurn && !vLegalMoves.empty())
				{
					const SCheckerMove &Chosen = vLegalMoves[rand() % vLegalMoves.size()];
					ApplyMove(Chosen);
					CheckEnd();
					vLegalMoves = s_Checkers.m_GameOver ? std::vector<SCheckerMove>() : CollectMoves(s_Checkers.m_Turn);
					vSelectedMoves.clear();
				}
				else if(Ui()->MouseInside(&Board) && Ui()->MouseButtonClicked(0))
				{
					const float CellSize = Board.w / 8.0f;
					const vec2 Mouse = Ui()->MousePos();
					const int X = std::clamp((int)((Mouse.x - Board.x) / CellSize), 0, 7);
					const int Y = std::clamp((int)((Mouse.y - Board.y) / CellSize), 0, 7);
					const int Clicked = ChIdx(X, Y);

					bool MoveApplied = false;
					for(const SCheckerMove &Move : vSelectedMoves)
					{
						if(Move.m_To == Clicked)
						{
							ApplyMove(Move);
							CheckEnd();
							MoveApplied = true;
							break;
						}
					}
					if(!MoveApplied)
					{
						if(s_Checkers.m_aBoard[Clicked] * s_Checkers.m_Turn > 0)
						{
							bool HasPieceMoves = false;
							for(const SCheckerMove &Move : vLegalMoves)
								if(Move.m_From == Clicked)
									HasPieceMoves = true;
							s_Checkers.m_Selected = HasPieceMoves ? Clicked : -1;
						}
						else
						{
							s_Checkers.m_Selected = -1;
						}
					}
				}
			}

			const float CellSize = Board.w / 8.0f;
			for(int y = 0; y < 8; ++y)
				for(int x = 0; x < 8; ++x)
				{
					CUIRect Cell;
					Cell.x = Board.x + x * CellSize;
					Cell.y = Board.y + y * CellSize;
					Cell.w = CellSize;
					Cell.h = CellSize;
					const bool Light = ((x + y) & 1) == 0;
					Cell.Draw(Light ? ColorRGBA(0.90f, 0.84f, 0.72f, 0.95f) : ColorRGBA(0.42f, 0.30f, 0.20f, 0.96f), IGraphics::CORNER_NONE, 0.0f);

					const int Pos = ChIdx(x, y);
					if(Pos == s_Checkers.m_Selected)
						Cell.Draw(ColorRGBA(0.30f, 0.72f, 1.0f, 0.25f), IGraphics::CORNER_NONE, 0.0f);
					for(const SCheckerMove &Move : vSelectedMoves)
						if(Move.m_To == Pos)
							Cell.Draw(ColorRGBA(0.35f, 1.0f, 0.45f, 0.22f), IGraphics::CORNER_NONE, 0.0f);

					const int Piece = s_Checkers.m_aBoard[Pos];
					if(Piece == 0)
						continue;

					CUIRect Disc = Cell;
					Disc.Margin(Cell.w * 0.14f, &Disc);
					const ColorRGBA PieceColor = Piece > 0 ? ColorRGBA(0.90f, 0.95f, 1.0f, 0.98f) : ColorRGBA(0.20f, 0.22f, 0.26f, 0.98f);
					Disc.Draw(PieceColor, IGraphics::CORNER_ALL, Disc.h * 0.5f);
					if(abs(Piece) == 2)
					{
						const ColorRGBA KingColor = Piece > 0 ? ColorRGBA(0.25f, 0.42f, 0.70f, 0.95f) : ColorRGBA(0.95f, 0.74f, 0.40f, 0.95f);
						RenderIconLabel(Disc, FONT_ICON_CHESS_KING, Disc.h * 0.46f, TEXTALIGN_MC, &KingColor);
					}
				}
	}
	else if(s_SelectedGame == FUN_GAME_BATTLESHIP)
	{
		struct SBattleshipState
		{
			int m_SizePreset = 1;
			bool m_Initialized = false;
			int m_Size = 10;
			std::vector<uint8_t> m_vShips;
			std::vector<uint8_t> m_vShots; // 0 unknown, 1 miss, 2 hit
			int m_TotalShipCells = 0;
			int m_Hits = 0;
			int m_Shots = 0;
			int m_BestShots = 0;
			bool m_Won = false;
		};

		static SBattleshipState s_Battle;

		auto BsIdx = [&](int X, int Y) {
			return Y * s_Battle.m_Size + X;
		};
		auto InBounds = [&](int X, int Y) {
			return X >= 0 && Y >= 0 && X < s_Battle.m_Size && Y < s_Battle.m_Size;
		};
		auto ResetBattleship = [&]() {
			s_Battle.m_Initialized = true;
			s_Battle.m_Size = s_Battle.m_SizePreset == 0 ? 8 : 10;
			s_Battle.m_vShips.assign((size_t)s_Battle.m_Size * s_Battle.m_Size, 0);
			s_Battle.m_vShots.assign((size_t)s_Battle.m_Size * s_Battle.m_Size, 0);
			s_Battle.m_Hits = 0;
			s_Battle.m_Shots = 0;
			s_Battle.m_Won = false;

			const std::vector<int> vShips = s_Battle.m_Size == 8 ? std::vector<int>{4, 3, 3, 2, 2, 1, 1} : std::vector<int>{5, 4, 3, 3, 2, 2, 2, 1, 1};
			bool PlacedAll = false;
			for(int GlobalTry = 0; GlobalTry < 200 && !PlacedAll; ++GlobalTry)
			{
				std::fill(s_Battle.m_vShips.begin(), s_Battle.m_vShips.end(), 0);
				bool Failed = false;
				for(int ShipLen : vShips)
				{
					bool Placed = false;
					for(int Try = 0; Try < 450 && !Placed; ++Try)
					{
						const bool Horizontal = rand() % 2 == 0;
						const int MaxX = Horizontal ? s_Battle.m_Size - ShipLen : s_Battle.m_Size - 1;
						const int MaxY = Horizontal ? s_Battle.m_Size - 1 : s_Battle.m_Size - ShipLen;
						const int X = rand() % (MaxX + 1);
						const int Y = rand() % (MaxY + 1);

						bool CanPlace = true;
						for(int i = 0; i < ShipLen && CanPlace; ++i)
						{
							const int CX = X + (Horizontal ? i : 0);
							const int CY = Y + (Horizontal ? 0 : i);
							if(s_Battle.m_vShips[BsIdx(CX, CY)] != 0)
							{
								CanPlace = false;
								break;
							}
							for(int ny = CY - 1; ny <= CY + 1 && CanPlace; ++ny)
								for(int nx = CX - 1; nx <= CX + 1; ++nx)
									if(InBounds(nx, ny) && s_Battle.m_vShips[BsIdx(nx, ny)] != 0)
										CanPlace = false;
						}
						if(!CanPlace)
							continue;

						for(int i = 0; i < ShipLen; ++i)
						{
							const int CX = X + (Horizontal ? i : 0);
							const int CY = Y + (Horizontal ? 0 : i);
							s_Battle.m_vShips[BsIdx(CX, CY)] = 1;
						}
						Placed = true;
					}
					if(!Placed)
					{
						Failed = true;
						break;
					}
				}
				if(!Failed)
					PlacedAll = true;
			}

			s_Battle.m_TotalShipCells = 0;
			for(uint8_t Cell : s_Battle.m_vShips)
				s_Battle.m_TotalShipCells += Cell ? 1 : 0;
		};

			if(!s_Battle.m_Initialized)
				ResetBattleship();

			CUIRect TopBar, BoardArea;
			GameContent.HSplitTop(LINE_SIZE * 1.2f, &TopBar, &BoardArea);
			BoardArea.HSplitTop(MARGIN_SMALL, nullptr, &BoardArea);

			CUIRect Stats, BtnArea, RestartButton;
			TopBar.VSplitLeft(440.0f, &Stats, &BtnArea);
			BtnArea.VSplitRight(110.0f, &BtnArea, &RestartButton);

			char aBest[32];
			if(s_Battle.m_BestShots > 0)
				str_format(aBest, sizeof(aBest), "%d", s_Battle.m_BestShots);
			else
				str_copy(aBest, "-", sizeof(aBest));
			char aStats[160];
			str_format(aStats, sizeof(aStats), "Hits: %d/%d   Shots: %d   Best: %s", s_Battle.m_Hits, s_Battle.m_TotalShipCells, s_Battle.m_Shots, aBest);
			Ui()->DoLabel(&Stats, aStats, FONT_SIZE, TEXTALIGN_ML);
			static CButtonContainer s_BattleRestartButton;
			if(DoButton_Menu(&s_BattleRestartButton, TCLocalize("Restart"), 0, &RestartButton))
				ResetBattleship();

			CUIRect Board;
			const float BoardSize = minimum(BoardArea.w, BoardArea.h);
			Board.w = BoardSize;
			Board.h = BoardSize;
			Board.x = BoardArea.x + (BoardArea.w - Board.w) * 0.5f;
			Board.y = BoardArea.y + (BoardArea.h - Board.h) * 0.5f;
			Board.Draw(ColorRGBA(0.03f, 0.09f, 0.18f, 0.95f), IGraphics::CORNER_ALL, 6.0f);

			const float CellSize = Board.w / s_Battle.m_Size;
			int HoverX = -1;
			int HoverY = -1;
			if(Ui()->MouseInside(&Board))
			{
				const vec2 Mouse = Ui()->MousePos();
				HoverX = std::clamp((int)((Mouse.x - Board.x) / CellSize), 0, s_Battle.m_Size - 1);
				HoverY = std::clamp((int)((Mouse.y - Board.y) / CellSize), 0, s_Battle.m_Size - 1);
			}

			if(!s_Battle.m_Won && HoverX >= 0 && HoverY >= 0 && Ui()->MouseButtonClicked(0))
			{
				const int Idx = BsIdx(HoverX, HoverY);
				if(s_Battle.m_vShots[Idx] == 0)
				{
					s_Battle.m_Shots++;
					if(s_Battle.m_vShips[Idx])
					{
						s_Battle.m_vShots[Idx] = 2;
						s_Battle.m_Hits++;
						if(s_Battle.m_Hits >= s_Battle.m_TotalShipCells)
						{
							s_Battle.m_Won = true;
							if(s_Battle.m_BestShots == 0 || s_Battle.m_Shots < s_Battle.m_BestShots)
								s_Battle.m_BestShots = s_Battle.m_Shots;
						}
					}
					else
					{
						s_Battle.m_vShots[Idx] = 1;
					}
				}
			}

			for(int y = 0; y < s_Battle.m_Size; ++y)
				for(int x = 0; x < s_Battle.m_Size; ++x)
				{
					const int Idx = BsIdx(x, y);
					CUIRect Cell;
					Cell.x = Board.x + x * CellSize;
					Cell.y = Board.y + y * CellSize;
					Cell.w = CellSize;
					Cell.h = CellSize;
					const float Pad = maximum(1.0f, CellSize * 0.05f);
					Cell.Margin(Pad, &Cell);

					ColorRGBA CellColor(0.09f, 0.20f, 0.35f, 0.95f);
					if(s_Battle.m_vShots[Idx] == 1)
						CellColor = ColorRGBA(0.30f, 0.46f, 0.63f, 0.96f);
					else if(s_Battle.m_vShots[Idx] == 2)
						CellColor = ColorRGBA(0.90f, 0.34f, 0.30f, 0.98f);
					else if(s_Battle.m_Won && s_Battle.m_vShips[Idx])
						CellColor = ColorRGBA(0.34f, 0.76f, 0.98f, 0.95f);
					else if(x == HoverX && y == HoverY)
						CellColor = BlendColors(CellColor, ColorRGBA(1.0f, 1.0f, 1.0f, 0.22f), 0.6f);
					Cell.Draw(CellColor, IGraphics::CORNER_ALL, 2.5f);

					if(s_Battle.m_vShots[Idx] == 1)
					{
						CUIRect Dot = Cell;
						Dot.Margin(Cell.w * 0.34f, &Dot);
						Dot.Draw(ColorRGBA(0.95f, 0.98f, 1.0f, 0.86f), IGraphics::CORNER_ALL, Dot.h * 0.5f);
					}
				}

			if(s_Battle.m_Won)
			{
				CUIRect Overlay = Board;
				Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.30f), IGraphics::CORNER_ALL, 6.0f);
				Ui()->DoLabel(&Overlay, TCLocalize("Fleet destroyed"), HEADLINE_FONT_SIZE, TEXTALIGN_MC);
			}
	}
	else if(s_SelectedGame == FUN_GAME_FLOWMANIA)
	{
		struct SFlowmaniaState
		{
			int m_Preset = 0;
			bool m_Initialized = false;
			int m_Size = 5;
			int m_ColorCount = 4;
			std::vector<int> m_vCells;
			std::vector<int> m_vEndpointA;
			std::vector<int> m_vEndpointB;
			std::vector<std::vector<int>> m_vPaths;
			int m_ActiveColor = 0;
			int m_Moves = 0;
			bool m_Won = false;
		};

		static SFlowmaniaState s_Flow;

		auto FlIdx = [&](int X, int Y) {
			return Y * s_Flow.m_Size + X;
		};
		auto FlAdj = [&](int A, int B) {
			const int AX = A % s_Flow.m_Size;
			const int AY = A / s_Flow.m_Size;
			const int BX = B % s_Flow.m_Size;
			const int BY = B / s_Flow.m_Size;
			return abs(AX - BX) + abs(AY - BY) == 1;
		};
		auto GetEndpointColor = [&](int Idx) {
			for(int c = 0; c < s_Flow.m_ColorCount; ++c)
				if(s_Flow.m_vEndpointA[c] == Idx || s_Flow.m_vEndpointB[c] == Idx)
					return c + 1;
			return 0;
		};
		auto ResetColorPath = [&](int Color) {
			if(Color <= 0 || Color > s_Flow.m_ColorCount)
				return;
			const int A = s_Flow.m_vEndpointA[Color - 1];
			const int B = s_Flow.m_vEndpointB[Color - 1];
			for(int &Cell : s_Flow.m_vCells)
				if(Cell == Color)
					Cell = 0;
			s_Flow.m_vCells[A] = Color;
			s_Flow.m_vCells[B] = Color;
			s_Flow.m_vPaths[Color - 1].clear();
			s_Flow.m_vPaths[Color - 1].push_back(A);
		};
		auto SetupFlowPreset = [&]() {
			if(s_Flow.m_Preset == 0)
			{
				s_Flow.m_Size = 5;
				s_Flow.m_ColorCount = 4;
			}
			else if(s_Flow.m_Preset == 1)
			{
				s_Flow.m_Size = 6;
				s_Flow.m_ColorCount = 5;
			}
			else
			{
				s_Flow.m_Size = 7;
				s_Flow.m_ColorCount = 6;
			}
			s_Flow.m_vCells.assign((size_t)s_Flow.m_Size * s_Flow.m_Size, 0);
			s_Flow.m_vEndpointA.assign(s_Flow.m_ColorCount, -1);
			s_Flow.m_vEndpointB.assign(s_Flow.m_ColorCount, -1);
			s_Flow.m_vPaths.assign(s_Flow.m_ColorCount, {});

			auto SetPair = [&](int Color, int AX, int AY, int BX, int BY) {
				const int A = FlIdx(AX, AY);
				const int B = FlIdx(BX, BY);
				s_Flow.m_vEndpointA[Color - 1] = A;
				s_Flow.m_vEndpointB[Color - 1] = B;
				s_Flow.m_vCells[A] = Color;
				s_Flow.m_vCells[B] = Color;
				s_Flow.m_vPaths[Color - 1].push_back(A);
			};

			if(s_Flow.m_Preset == 0)
			{
				SetPair(1, 0, 0, 4, 0);
				SetPair(2, 0, 4, 4, 4);
				SetPair(3, 1, 1, 3, 3);
				SetPair(4, 0, 2, 4, 2);
			}
			else if(s_Flow.m_Preset == 1)
			{
				SetPair(1, 0, 0, 5, 1);
				SetPair(2, 1, 5, 5, 5);
				SetPair(3, 0, 3, 3, 0);
				SetPair(4, 2, 2, 5, 3);
				SetPair(5, 0, 5, 4, 4);
			}
			else
			{
				SetPair(1, 0, 0, 6, 0);
				SetPair(2, 0, 6, 6, 6);
				SetPair(3, 1, 2, 5, 4);
				SetPair(4, 0, 3, 6, 3);
				SetPair(5, 2, 1, 4, 5);
				SetPair(6, 2, 5, 5, 2);
			}
		};
		auto ResetFlowmania = [&]() {
			s_Flow.m_Initialized = true;
			s_Flow.m_ActiveColor = 0;
			s_Flow.m_Moves = 0;
			s_Flow.m_Won = false;
			SetupFlowPreset();
		};
		auto CheckFlowWin = [&]() {
			for(int c = 0; c < s_Flow.m_ColorCount; ++c)
			{
				if(s_Flow.m_vPaths[c].empty())
					return false;
				if(s_Flow.m_vPaths[c].back() != s_Flow.m_vEndpointB[c])
					return false;
			}
			return true;
		};

			if(!s_Flow.m_Initialized)
				ResetFlowmania();

			CUIRect TopBar, BoardArea;
			GameContent.HSplitTop(LINE_SIZE * 1.2f, &TopBar, &BoardArea);
			BoardArea.HSplitTop(MARGIN_SMALL, nullptr, &BoardArea);

			CUIRect Status, BtnArea, RestartButton;
			TopBar.VSplitLeft(420.0f, &Status, &BtnArea);
			BtnArea.VSplitRight(110.0f, &BtnArea, &RestartButton);

			char aStatus[180];
			str_format(aStatus, sizeof(aStatus), "Active color: %d   Moves: %d", s_Flow.m_ActiveColor, s_Flow.m_Moves);
			Ui()->DoLabel(&Status, aStatus, FONT_SIZE, TEXTALIGN_ML);
			static CButtonContainer s_FlowRestartButton;
			if(DoButton_Menu(&s_FlowRestartButton, TCLocalize("Restart"), 0, &RestartButton))
				ResetFlowmania();

			CUIRect Board;
			const float BoardSize = minimum(BoardArea.w, BoardArea.h);
			Board.w = BoardSize;
			Board.h = BoardSize;
			Board.x = BoardArea.x + (BoardArea.w - Board.w) * 0.5f;
			Board.y = BoardArea.y + (BoardArea.h - Board.h) * 0.5f;
			Board.Draw(ColorRGBA(0.04f, 0.08f, 0.15f, 0.95f), IGraphics::CORNER_ALL, 6.0f);

			const ColorRGBA aColors[6] = {
				ColorRGBA(0.95f, 0.34f, 0.34f, 0.95f),
				ColorRGBA(0.36f, 0.78f, 1.0f, 0.95f),
				ColorRGBA(1.0f, 0.84f, 0.33f, 0.95f),
				ColorRGBA(0.46f, 0.92f, 0.52f, 0.95f),
				ColorRGBA(0.88f, 0.56f, 1.0f, 0.95f),
				ColorRGBA(1.0f, 0.58f, 0.30f, 0.95f)};

			const float CellSize = Board.w / s_Flow.m_Size;
			int HoverX = -1;
			int HoverY = -1;
			if(Ui()->MouseInside(&Board))
			{
				const vec2 Mouse = Ui()->MousePos();
				HoverX = std::clamp((int)((Mouse.x - Board.x) / CellSize), 0, s_Flow.m_Size - 1);
				HoverY = std::clamp((int)((Mouse.y - Board.y) / CellSize), 0, s_Flow.m_Size - 1);
			}

			if(!s_Flow.m_Won && HoverX >= 0 && HoverY >= 0 && Ui()->MouseButtonClicked(0))
			{
				const int Clicked = FlIdx(HoverX, HoverY);
				const int EndpointColor = GetEndpointColor(Clicked);
				if(EndpointColor > 0)
				{
					if(s_Flow.m_ActiveColor == EndpointColor && Clicked == s_Flow.m_vEndpointA[EndpointColor - 1] && s_Flow.m_vPaths[EndpointColor - 1].size() > 1)
					{
						ResetColorPath(EndpointColor);
						s_Flow.m_Moves++;
					}
					s_Flow.m_ActiveColor = EndpointColor;
				}
				else if(s_Flow.m_ActiveColor > 0)
				{
					std::vector<int> &vPath = s_Flow.m_vPaths[s_Flow.m_ActiveColor - 1];
					if(!vPath.empty())
					{
						const int Tail = vPath.back();
						if(vPath.size() >= 2 && Clicked == vPath[vPath.size() - 2])
						{
							const int Endpoint = GetEndpointColor(Tail);
							if(Endpoint == 0)
								s_Flow.m_vCells[Tail] = 0;
							vPath.pop_back();
							s_Flow.m_Moves++;
						}
						else if(FlAdj(Tail, Clicked))
						{
							const int CellColor = s_Flow.m_vCells[Clicked];
							const int Endpoint = GetEndpointColor(Clicked);
							if(Endpoint > 0 && Endpoint != s_Flow.m_ActiveColor)
							{
								// blocked by another color endpoint
							}
							else if(Endpoint == s_Flow.m_ActiveColor)
							{
								if(Clicked == s_Flow.m_vEndpointB[s_Flow.m_ActiveColor - 1])
								{
									if(vPath.back() != Clicked)
									{
										vPath.push_back(Clicked);
										s_Flow.m_Moves++;
									}
								}
							}
							else if(CellColor == 0)
							{
								s_Flow.m_vCells[Clicked] = s_Flow.m_ActiveColor;
								vPath.push_back(Clicked);
								s_Flow.m_Moves++;
							}
						}
					}
				}
				s_Flow.m_Won = CheckFlowWin();
			}

			for(int c = 0; c < s_Flow.m_ColorCount; ++c)
			{
				const ColorRGBA &Col = aColors[c % 6];
				const std::vector<int> &vPath = s_Flow.m_vPaths[c];
				if(vPath.size() < 2)
					continue;
				Graphics()->TextureClear();
				Graphics()->LinesBegin();
				Graphics()->SetColor(Col.r, Col.g, Col.b, 0.92f);
				for(size_t i = 1; i < vPath.size(); ++i)
				{
					const int A = vPath[i - 1];
					const int B = vPath[i];
					const vec2 PA(Board.x + (A % s_Flow.m_Size + 0.5f) * CellSize, Board.y + (A / s_Flow.m_Size + 0.5f) * CellSize);
					const vec2 PB(Board.x + (B % s_Flow.m_Size + 0.5f) * CellSize, Board.y + (B / s_Flow.m_Size + 0.5f) * CellSize);
					IGraphics::CLineItem Line(PA.x, PA.y, PB.x, PB.y);
					Graphics()->LinesDraw(&Line, 1);
				}
				Graphics()->LinesEnd();
			}

			for(int y = 0; y < s_Flow.m_Size; ++y)
				for(int x = 0; x < s_Flow.m_Size; ++x)
				{
					const int Idx = FlIdx(x, y);
					CUIRect Cell;
					Cell.x = Board.x + x * CellSize;
					Cell.y = Board.y + y * CellSize;
					Cell.w = CellSize;
					Cell.h = CellSize;
					Cell.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.03f), IGraphics::CORNER_NONE, 0.0f);

					const int Color = s_Flow.m_vCells[Idx];
					if(Color > 0)
					{
						CUIRect Fill = Cell;
						Fill.Margin(Cell.w * 0.18f, &Fill);
						Fill.Draw(aColors[(Color - 1) % 6], IGraphics::CORNER_ALL, Fill.h * 0.35f);
					}

					const int EndpointColor = GetEndpointColor(Idx);
					if(EndpointColor > 0)
					{
						CUIRect Dot = Cell;
						Dot.Margin(Cell.w * 0.28f, &Dot);
						const ColorRGBA Col = aColors[(EndpointColor - 1) % 6];
						Dot.Draw(BlendColors(Col, ColorRGBA(1.0f, 1.0f, 1.0f, 0.95f), 0.32f), IGraphics::CORNER_ALL, Dot.h * 0.5f);
						if(s_Flow.m_ActiveColor == EndpointColor)
							Dot.DrawOutline(ColorRGBA(1.0f, 1.0f, 1.0f, 0.55f));
					}
					else if(x == HoverX && y == HoverY)
					{
						Cell.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), IGraphics::CORNER_NONE, 0.0f);
					}
				}

			if(s_Flow.m_Won)
			{
				CUIRect Overlay = Board;
				Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.34f), IGraphics::CORNER_ALL, 6.0f);
				Ui()->DoLabel(&Overlay, TCLocalize("All pairs connected"), HEADLINE_FONT_SIZE, TEXTALIGN_MC);
			}
	}
	else if(s_SelectedGame == FUN_GAME_UNO)
	{
		struct SUnoState
		{
			bool m_Initialized = false;
			std::vector<int> m_vDeck;
			std::vector<int> m_vPlayer;
			std::vector<int> m_vBot;
			int m_TopCard = 0;
			bool m_PlayerTurn = true;
			bool m_GameOver = false;
			int m_Winner = 0; // 1 player, 2 bot
			int m_PlayerWins = 0;
			int m_BotWins = 0;
			int64_t m_BotMoveAt = 0;
		};

		static SUnoState s_Uno;
		static CButtonContainer s_UnoDrawButton;

		auto CardColor = [&](int Card) {
			return Card / 10;
		};
		auto CardValue = [&](int Card) {
			return Card % 10;
		};
		auto CanPlayCard = [&](int Card) {
			return CardColor(Card) == CardColor(s_Uno.m_TopCard) || CardValue(Card) == CardValue(s_Uno.m_TopCard);
		};
		auto BuildDeck = [&]() {
			s_Uno.m_vDeck.clear();
			for(int Color = 0; Color < 4; ++Color)
				for(int Value = 0; Value <= 9; ++Value)
				{
					s_Uno.m_vDeck.push_back(Color * 10 + Value);
					s_Uno.m_vDeck.push_back(Color * 10 + Value);
				}
			ShuffleVector(s_Uno.m_vDeck);
		};
		auto DrawCard = [&](std::vector<int> &vHand) {
			if(s_Uno.m_vDeck.empty())
				return -1;
			const int Card = s_Uno.m_vDeck.back();
			s_Uno.m_vDeck.pop_back();
			vHand.push_back(Card);
			return Card;
		};
		auto ResetUno = [&]() {
			s_Uno.m_Initialized = true;
			BuildDeck();
			s_Uno.m_vPlayer.clear();
			s_Uno.m_vBot.clear();
			for(int i = 0; i < 7; ++i)
			{
				DrawCard(s_Uno.m_vPlayer);
				DrawCard(s_Uno.m_vBot);
			}
			s_Uno.m_TopCard = DrawCard(s_Uno.m_vPlayer);
			s_Uno.m_vPlayer.pop_back();
			s_Uno.m_PlayerTurn = rand() % 2 == 0;
			s_Uno.m_GameOver = false;
			s_Uno.m_Winner = 0;
			s_Uno.m_BotMoveAt = time_get() + (int64_t)(time_freq() * 0.45f);
		};
		auto FinishUnoRound = [&](int Winner) {
			s_Uno.m_GameOver = true;
			s_Uno.m_Winner = Winner;
			if(Winner == 1)
				s_Uno.m_PlayerWins++;
			else
				s_Uno.m_BotWins++;
		};

			if(!s_Uno.m_Initialized)
				ResetUno();

			const ColorRGBA aCardColors[4] = {
				ColorRGBA(0.95f, 0.34f, 0.33f, 0.95f),
				ColorRGBA(1.0f, 0.84f, 0.31f, 0.95f),
				ColorRGBA(0.34f, 0.82f, 0.45f, 0.95f),
				ColorRGBA(0.32f, 0.62f, 1.0f, 0.95f)};
			const char aColorChars[4] = {'R', 'Y', 'G', 'B'};

			CUIRect TopBar, ArenaArea;
			GameContent.HSplitTop(LINE_SIZE * 1.2f, &TopBar, &ArenaArea);
			ArenaArea.HSplitTop(MARGIN_SMALL, nullptr, &ArenaArea);

			CUIRect Status, BtnArea, DrawButton, RestartButton;
			TopBar.VSplitLeft(360.0f, &Status, &BtnArea);
			BtnArea.VSplitRight(220.0f, &BtnArea, &RestartButton);
			BtnArea.VSplitRight(110.0f, &BtnArea, &DrawButton);
			RestartButton.VSplitLeft(10.0f, nullptr, &RestartButton);

			char aStatus[160];
			str_format(aStatus, sizeof(aStatus), "Your cards: %d   Bot cards: %d   Score %d:%d", (int)s_Uno.m_vPlayer.size(), (int)s_Uno.m_vBot.size(), s_Uno.m_PlayerWins, s_Uno.m_BotWins);
			Ui()->DoLabel(&Status, aStatus, FONT_SIZE, TEXTALIGN_ML);

			static CButtonContainer s_UnoRestartButton;
			if(DoButton_Menu(&s_UnoRestartButton, TCLocalize("Restart"), 0, &RestartButton))
				ResetUno();

			bool DrawPressed = false;
			if(DoButton_Menu(&s_UnoDrawButton, TCLocalize("Draw"), 0, &DrawButton))
				DrawPressed = true;

			CUIRect Arena = ArenaArea;
			Arena.Draw(ColorRGBA(0.04f, 0.08f, 0.14f, 0.95f), IGraphics::CORNER_ALL, 8.0f);

			if(!s_Uno.m_GameOver)
			{
				const int64_t Now = time_get();
				if(s_Uno.m_PlayerTurn)
				{
					if(DrawPressed)
					{
						const int Card = DrawCard(s_Uno.m_vPlayer);
						if(Card >= 0 && CanPlayCard(Card))
						{
							s_Uno.m_vPlayer.pop_back();
							s_Uno.m_TopCard = Card;
							if(s_Uno.m_vPlayer.empty())
								FinishUnoRound(1);
							else
							{
								s_Uno.m_PlayerTurn = false;
								s_Uno.m_BotMoveAt = Now + (int64_t)(time_freq() * 0.45f);
							}
						}
						else
						{
							s_Uno.m_PlayerTurn = false;
							s_Uno.m_BotMoveAt = Now + (int64_t)(time_freq() * 0.45f);
						}
					}
				}
				else if(Now >= s_Uno.m_BotMoveAt)
				{
					int PlayIdx = -1;
					for(int i = 0; i < (int)s_Uno.m_vBot.size(); ++i)
						if(CanPlayCard(s_Uno.m_vBot[i]))
						{
							PlayIdx = i;
							break;
						}
					if(PlayIdx < 0)
					{
						const int Card = DrawCard(s_Uno.m_vBot);
						if(Card >= 0 && CanPlayCard(Card))
							PlayIdx = (int)s_Uno.m_vBot.size() - 1;
					}
					if(PlayIdx >= 0)
					{
						s_Uno.m_TopCard = s_Uno.m_vBot[PlayIdx];
						s_Uno.m_vBot.erase(s_Uno.m_vBot.begin() + PlayIdx);
						if(s_Uno.m_vBot.empty())
						{
							FinishUnoRound(2);
						}
						else
						{
							s_Uno.m_PlayerTurn = true;
						}
					}
					else
					{
						s_Uno.m_PlayerTurn = true;
					}
				}
			}

			CUIRect Center = Arena;
			CUIRect TopHand, Mid, BottomHand;
			Center.HSplitTop(Arena.h * 0.26f, &TopHand, &Center);
			Center.HSplitBottom(Arena.h * 0.32f, &Center, &BottomHand);
			Mid = Center;

			const float CardH = minimum(84.0f, Arena.h * 0.23f);
			const float CardW = CardH * 0.65f;

			// Bot hand (card backs)
			if(!s_Uno.m_vBot.empty())
			{
				const float Gap = minimum(20.0f, (TopHand.w - CardW) / maximum(1.0f, (float)s_Uno.m_vBot.size() - 1.0f));
				const float TotalW = CardW + (s_Uno.m_vBot.size() - 1) * Gap;
				const float StartX = TopHand.x + (TopHand.w - TotalW) * 0.5f;
				for(int i = 0; i < (int)s_Uno.m_vBot.size(); ++i)
				{
					CUIRect Card;
					Card.x = StartX + i * Gap;
					Card.y = TopHand.y + (TopHand.h - CardH) * 0.5f;
					Card.w = CardW;
					Card.h = CardH;
					Card.Draw(ColorRGBA(0.13f, 0.16f, 0.24f, 0.95f), IGraphics::CORNER_ALL, 4.0f);
					CUIRect Dot = Card;
					Dot.Margin(Card.w * 0.32f, &Dot);
					Dot.Draw(ColorRGBA(0.92f, 0.95f, 1.0f, 0.35f), IGraphics::CORNER_ALL, Dot.h * 0.5f);
				}
			}

			// Discard pile
			CUIRect TopCardRect;
			TopCardRect.w = CardW * 1.15f;
			TopCardRect.h = CardH * 1.15f;
			TopCardRect.x = Mid.x + (Mid.w - TopCardRect.w) * 0.5f;
			TopCardRect.y = Mid.y + (Mid.h - TopCardRect.h) * 0.5f;
			const int TopColor = CardColor(s_Uno.m_TopCard);
			TopCardRect.Draw(aCardColors[TopColor], IGraphics::CORNER_ALL, 6.0f);
			char aTop[16];
			str_format(aTop, sizeof(aTop), "%c%d", aColorChars[TopColor], CardValue(s_Uno.m_TopCard));
			Ui()->DoLabel(&TopCardRect, aTop, TopCardRect.h * 0.38f, TEXTALIGN_MC);

			// Player hand
			int ClickedCard = -1;
			if(!s_Uno.m_vPlayer.empty())
			{
				const float Gap = minimum(46.0f, (BottomHand.w - CardW) / maximum(1.0f, (float)s_Uno.m_vPlayer.size() - 1.0f));
				const float TotalW = CardW + (s_Uno.m_vPlayer.size() - 1) * Gap;
				const float StartX = BottomHand.x + (BottomHand.w - TotalW) * 0.5f;
				for(int i = 0; i < (int)s_Uno.m_vPlayer.size(); ++i)
				{
					const int CardValueRaw = s_Uno.m_vPlayer[i];
					const int Col = CardColor(CardValueRaw);
					CUIRect Card;
					Card.x = StartX + i * Gap;
					Card.y = BottomHand.y + (BottomHand.h - CardH) * 0.5f;
					Card.w = CardW;
					Card.h = CardH;
					ColorRGBA CardColorValue = aCardColors[Col];
					if(!CanPlayCard(CardValueRaw))
						CardColorValue = BlendColors(CardColorValue, ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), 0.45f);
					if(Ui()->MouseInside(&Card))
					{
						Card.y -= Card.h * 0.08f;
						CardColorValue = BlendColors(CardColorValue, ColorRGBA(1.0f, 1.0f, 1.0f, 0.22f), 0.55f);
						if(Ui()->MouseButtonClicked(0))
							ClickedCard = i;
					}
					Card.Draw(CardColorValue, IGraphics::CORNER_ALL, 4.0f);
					char aCardLabel[16];
					str_format(aCardLabel, sizeof(aCardLabel), "%c%d", aColorChars[Col], CardValue(CardValueRaw));
					Ui()->DoLabel(&Card, aCardLabel, Card.h * 0.33f, TEXTALIGN_MC);
				}
			}

			if(!s_Uno.m_GameOver && s_Uno.m_PlayerTurn && ClickedCard >= 0)
			{
				const int Card = s_Uno.m_vPlayer[ClickedCard];
				if(CanPlayCard(Card))
				{
					s_Uno.m_TopCard = Card;
					s_Uno.m_vPlayer.erase(s_Uno.m_vPlayer.begin() + ClickedCard);
					if(s_Uno.m_vPlayer.empty())
						FinishUnoRound(1);
					else
					{
						s_Uno.m_PlayerTurn = false;
						s_Uno.m_BotMoveAt = time_get() + (int64_t)(time_freq() * 0.45f);
					}
				}
			}

			if(s_Uno.m_GameOver)
			{
				CUIRect Overlay = Arena;
				Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.34f), IGraphics::CORNER_ALL, 8.0f);
				Ui()->DoLabel(&Overlay, s_Uno.m_Winner == 1 ? TCLocalize("You win") : TCLocalize("Bot wins"), HEADLINE_FONT_SIZE, TEXTALIGN_MC);
			}
	}
	else if(s_SelectedGame == FUN_GAME_BILLIARDS)
	{
		struct SBilliardBall
		{
			vec2 m_Pos = vec2(0.0f, 0.0f);
			vec2 m_Vel = vec2(0.0f, 0.0f);
			ColorRGBA m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
			bool m_Active = true;
			bool m_Cue = false;
		};

		struct SBilliardsState
		{
			int m_BallPreset = 1;
			bool m_Initialized = false;
			std::vector<SBilliardBall> m_vBalls;
			int m_Remaining = 0;
			int m_Shots = 0;
			int m_BestShots = 0;
			bool m_Won = false;
			vec2 m_AimDir = vec2(1.0f, 0.0f);
			float m_AimPower = 0.55f;
			int64_t m_LastTick = 0;
		};

		static SBilliardsState s_Billiards;

		auto GetCueBall = [&]() -> SBilliardBall * {
			for(SBilliardBall &Ball : s_Billiards.m_vBalls)
				if(Ball.m_Cue)
					return &Ball;
			return nullptr;
		};
		auto BallsMoving = [&]() {
			for(const SBilliardBall &Ball : s_Billiards.m_vBalls)
			{
				if(!Ball.m_Active)
					continue;
				if(Ball.m_Vel.x * Ball.m_Vel.x + Ball.m_Vel.y * Ball.m_Vel.y > 0.0006f)
					return true;
			}
			return false;
		};
		auto ResetBilliards = [&]() {
			s_Billiards.m_Initialized = true;
			s_Billiards.m_vBalls.clear();
			s_Billiards.m_Shots = 0;
			s_Billiards.m_Won = false;
			s_Billiards.m_AimDir = vec2(1.0f, 0.0f);
			s_Billiards.m_AimPower = 0.55f;
			s_Billiards.m_LastTick = time_get();

			const int ObjectCount = s_Billiards.m_BallPreset == 0 ? 5 : s_Billiards.m_BallPreset == 1 ? 8 :
														    12;
			static const ColorRGBA s_aPalette[12] = {
				ColorRGBA(0.95f, 0.86f, 0.23f, 0.95f), ColorRGBA(0.34f, 0.70f, 1.0f, 0.95f), ColorRGBA(1.0f, 0.43f, 0.34f, 0.95f), ColorRGBA(0.60f, 0.45f, 1.0f, 0.95f),
				ColorRGBA(0.44f, 0.90f, 0.58f, 0.95f), ColorRGBA(1.0f, 0.63f, 0.26f, 0.95f), ColorRGBA(0.92f, 0.35f, 0.66f, 0.95f), ColorRGBA(0.24f, 0.26f, 0.30f, 0.95f),
				ColorRGBA(0.84f, 0.84f, 0.88f, 0.95f), ColorRGBA(0.94f, 0.54f, 0.24f, 0.95f), ColorRGBA(0.32f, 0.86f, 0.86f, 0.95f), ColorRGBA(0.98f, 0.76f, 0.36f, 0.95f)};

			SBilliardBall Cue;
			Cue.m_Cue = true;
			Cue.m_Color = ColorRGBA(0.98f, 0.99f, 1.0f, 0.98f);
			Cue.m_Pos = vec2(0.22f, 0.5f);
			s_Billiards.m_vBalls.push_back(Cue);

			int Placed = 0;
			int Row = 0;
			const float Gap = 0.047f;
			while(Placed < ObjectCount)
			{
				for(int i = 0; i <= Row && Placed < ObjectCount; ++i)
				{
					SBilliardBall Ball;
					Ball.m_Cue = false;
					Ball.m_Color = s_aPalette[Placed % 12];
					Ball.m_Pos.x = 0.70f + Row * Gap * 0.92f;
					Ball.m_Pos.y = 0.5f + (i - Row * 0.5f) * Gap;
					s_Billiards.m_vBalls.push_back(Ball);
					Placed++;
				}
				Row++;
			}
			s_Billiards.m_Remaining = ObjectCount;
		};

			if(!s_Billiards.m_Initialized)
				ResetBilliards();

			CUIRect TopBar, ArenaArea;
			GameContent.HSplitTop(LINE_SIZE * 1.2f, &TopBar, &ArenaArea);
			ArenaArea.HSplitTop(MARGIN_SMALL, nullptr, &ArenaArea);

			CUIRect Stats, BtnArea, RestartButton;
			TopBar.VSplitLeft(420.0f, &Stats, &BtnArea);
			BtnArea.VSplitRight(110.0f, &BtnArea, &RestartButton);

			char aBest[32];
			if(s_Billiards.m_BestShots > 0)
				str_format(aBest, sizeof(aBest), "%d", s_Billiards.m_BestShots);
			else
				str_copy(aBest, "-", sizeof(aBest));
			char aStats[180];
			str_format(aStats, sizeof(aStats), "Remaining: %d   Shots: %d   Best: %s", s_Billiards.m_Remaining, s_Billiards.m_Shots, aBest);
			Ui()->DoLabel(&Stats, aStats, FONT_SIZE, TEXTALIGN_ML);
			static CButtonContainer s_BilliardsRestartButton;
			if(DoButton_Menu(&s_BilliardsRestartButton, TCLocalize("Restart"), 0, &RestartButton))
				ResetBilliards();

			CUIRect Table = ArenaArea;
			const float PreferredW = minimum(ArenaArea.w, ArenaArea.h * 2.1f);
			Table.w = PreferredW;
			Table.h = PreferredW / 2.1f;
			Table.x = ArenaArea.x + (ArenaArea.w - Table.w) * 0.5f;
			Table.y = ArenaArea.y + (ArenaArea.h - Table.h) * 0.5f;
			Table.Draw(ColorRGBA(0.14f, 0.46f, 0.23f, 0.96f), IGraphics::CORNER_ALL, 10.0f);
			Table.DrawOutline(ColorRGBA(0.25f, 0.19f, 0.10f, 0.80f));

			const int64_t Now = time_get();
			float Dt = (Now - s_Billiards.m_LastTick) / (float)time_freq();
			s_Billiards.m_LastTick = Now;
			Dt = std::clamp(Dt, 0.0f, 0.05f);

			const float BallRadius = 0.020f;
			const float PocketRadius = 0.048f;
			const vec2 aPockets[6] = {vec2(0.0f, 0.0f), vec2(0.5f, 0.0f), vec2(1.0f, 0.0f), vec2(0.0f, 1.0f), vec2(0.5f, 1.0f), vec2(1.0f, 1.0f)};

			if(!s_Billiards.m_Won)
			{
				if(BallsMoving())
				{
					float Remaining = Dt;
					while(Remaining > 0.0f)
					{
						const float Step = minimum(0.008f, Remaining);
						Remaining -= Step;

						for(SBilliardBall &Ball : s_Billiards.m_vBalls)
						{
							if(!Ball.m_Active)
								continue;
							Ball.m_Pos += Ball.m_Vel * Step;
							if(Ball.m_Pos.x < BallRadius)
							{
								Ball.m_Pos.x = BallRadius;
								Ball.m_Vel.x = fabs(Ball.m_Vel.x) * 0.93f;
							}
							else if(Ball.m_Pos.x > 1.0f - BallRadius)
							{
								Ball.m_Pos.x = 1.0f - BallRadius;
								Ball.m_Vel.x = -fabs(Ball.m_Vel.x) * 0.93f;
							}
							if(Ball.m_Pos.y < BallRadius)
							{
								Ball.m_Pos.y = BallRadius;
								Ball.m_Vel.y = fabs(Ball.m_Vel.y) * 0.93f;
							}
							else if(Ball.m_Pos.y > 1.0f - BallRadius)
							{
								Ball.m_Pos.y = 1.0f - BallRadius;
								Ball.m_Vel.y = -fabs(Ball.m_Vel.y) * 0.93f;
							}
						}

						for(size_t i = 0; i < s_Billiards.m_vBalls.size(); ++i)
							for(size_t j = i + 1; j < s_Billiards.m_vBalls.size(); ++j)
							{
								SBilliardBall &A = s_Billiards.m_vBalls[i];
								SBilliardBall &B = s_Billiards.m_vBalls[j];
								if(!A.m_Active || !B.m_Active)
									continue;
								const vec2 Diff = B.m_Pos - A.m_Pos;
								const float DistSq = Diff.x * Diff.x + Diff.y * Diff.y;
								const float MinDist = BallRadius * 2.0f;
								if(DistSq <= 0.000001f || DistSq >= MinDist * MinDist)
									continue;
								const float Dist = sqrtf(DistSq);
								const vec2 N = Diff * (1.0f / Dist);
								const float Overlap = MinDist - Dist;
								A.m_Pos -= N * (Overlap * 0.5f);
								B.m_Pos += N * (Overlap * 0.5f);
								const vec2 Rel = B.m_Vel - A.m_Vel;
								const float Along = Rel.x * N.x + Rel.y * N.y;
								if(Along < 0.0f)
								{
									const float Impulse = -(1.0f + 0.92f) * Along * 0.5f;
									A.m_Vel -= N * Impulse;
									B.m_Vel += N * Impulse;
								}
							}

						for(SBilliardBall &Ball : s_Billiards.m_vBalls)
						{
							if(!Ball.m_Active)
								continue;
							for(const vec2 &Pocket : aPockets)
							{
								const float DX = Ball.m_Pos.x - Pocket.x;
								const float DY = Ball.m_Pos.y - Pocket.y;
								if(DX * DX + DY * DY > PocketRadius * PocketRadius)
									continue;
								if(Ball.m_Cue)
								{
									Ball.m_Pos = vec2(0.22f, 0.5f);
									Ball.m_Vel = vec2(0.0f, 0.0f);
								}
								else
								{
									Ball.m_Active = false;
									s_Billiards.m_Remaining--;
								}
								break;
							}
						}

						for(SBilliardBall &Ball : s_Billiards.m_vBalls)
						{
							if(!Ball.m_Active)
								continue;
							Ball.m_Vel *= maximum(0.0f, 1.0f - Step * 1.2f);
							if(Ball.m_Vel.x * Ball.m_Vel.x + Ball.m_Vel.y * Ball.m_Vel.y < 0.0006f)
								Ball.m_Vel = vec2(0.0f, 0.0f);
						}
					}
				}
				else
				{
					SBilliardBall *pCue = GetCueBall();
					if(pCue != nullptr)
					{
						if(Ui()->MouseInside(&Table))
						{
							const vec2 Mouse = Ui()->MousePos();
							const vec2 CuePx(Table.x + pCue->m_Pos.x * Table.w, Table.y + pCue->m_Pos.y * Table.h);
							vec2 Dir(Mouse.x - CuePx.x, Mouse.y - CuePx.y);
							const float Len = sqrtf(Dir.x * Dir.x + Dir.y * Dir.y);
							if(Len > 1.0f)
								s_Billiards.m_AimDir = Dir * (1.0f / Len);
						}
						if(Input()->KeyIsPressed(KEY_UP) || Input()->KeyIsPressed(KEY_W))
							s_Billiards.m_AimPower = minimum(1.0f, s_Billiards.m_AimPower + Dt * 0.9f);
						if(Input()->KeyIsPressed(KEY_DOWN) || Input()->KeyIsPressed(KEY_S))
							s_Billiards.m_AimPower = maximum(0.15f, s_Billiards.m_AimPower - Dt * 0.9f);

						const bool Shoot = Input()->KeyPress(KEY_SPACE) || (Ui()->MouseInside(&Table) && Ui()->MouseButtonClicked(0));
						if(Shoot)
						{
							const float Force = 0.45f + s_Billiards.m_AimPower * 1.15f;
							pCue->m_Vel = s_Billiards.m_AimDir * Force;
							s_Billiards.m_Shots++;
						}
					}
				}

				if(s_Billiards.m_Remaining <= 0)
				{
					s_Billiards.m_Won = true;
					if(s_Billiards.m_BestShots == 0 || s_Billiards.m_Shots < s_Billiards.m_BestShots)
						s_Billiards.m_BestShots = s_Billiards.m_Shots;
				}
			}

			// Pockets
			for(const vec2 &Pocket : aPockets)
			{
				CUIRect P;
				P.w = Table.h * 0.13f;
				P.h = Table.h * 0.13f;
				P.x = Table.x + Pocket.x * Table.w - P.w * 0.5f;
				P.y = Table.y + Pocket.y * Table.h - P.h * 0.5f;
				P.Draw(ColorRGBA(0.03f, 0.04f, 0.05f, 0.95f), IGraphics::CORNER_ALL, P.h * 0.5f);
			}

			// Balls
			for(const SBilliardBall &Ball : s_Billiards.m_vBalls)
			{
				if(!Ball.m_Active)
					continue;
				CUIRect R;
				R.w = Table.h * 0.085f;
				R.h = Table.h * 0.085f;
				R.x = Table.x + Ball.m_Pos.x * Table.w - R.w * 0.5f;
				R.y = Table.y + Ball.m_Pos.y * Table.h - R.h * 0.5f;
				R.Draw(Ball.m_Color, IGraphics::CORNER_ALL, R.h * 0.5f);
				if(Ball.m_Cue)
				{
					CUIRect Dot = R;
					Dot.Margin(R.w * 0.34f, &Dot);
					Dot.Draw(ColorRGBA(0.08f, 0.08f, 0.10f, 0.55f), IGraphics::CORNER_ALL, Dot.h * 0.5f);
				}
			}

			if(!s_Billiards.m_Won && !BallsMoving())
			{
				SBilliardBall *pCue = GetCueBall();
				if(pCue != nullptr)
				{
					const vec2 Start(Table.x + pCue->m_Pos.x * Table.w, Table.y + pCue->m_Pos.y * Table.h);
					const vec2 End = Start + s_Billiards.m_AimDir * (Table.h * (0.15f + s_Billiards.m_AimPower * 0.26f));
					Graphics()->TextureClear();
					Graphics()->LinesBegin();
					Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.40f);
					IGraphics::CLineItem Line(Start.x, Start.y, End.x, End.y);
					Graphics()->LinesDraw(&Line, 1);
					Graphics()->LinesEnd();
				}
			}

			if(s_Billiards.m_Won)
			{
				CUIRect Overlay = Table;
				Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f), IGraphics::CORNER_ALL, 10.0f);
				Ui()->DoLabel(&Overlay, TCLocalize("Table cleared"), HEADLINE_FONT_SIZE, TEXTALIGN_MC);
			}
	}

	else if(s_SelectedGame == FUN_GAME_CASINO)
	{
		struct SCasinoState
		{
			int m_aSymbols[7] = {};
			bool m_aLocked[7] = {};
			float m_aScrollY[7] = {};
			int m_aScrollSym[7] = {};
			int m_MultiplierIdx = 0;
			bool m_Spinning = false;
			float m_SpinTimer = 0.f;
			bool m_ShowResult = false;
			bool m_Won = false;
			int m_WinAmount = 0;
			float m_ResultTimer = 0.f;
			int m_SpinStreak = 0;
		};
		static SCasinoState s_Casino;
		static CButtonContainer s_SpinBtn;
		static CButtonContainer s_ClaimBtn;
		static CButtonContainer s_aMulBtn[5];

		struct SSymbol { const char *m_pIcon; ColorRGBA m_Color; const char *m_pName; int m_Payout; bool m_IsIcon; };
		static const SSymbol s_aSym[6] = {
			{"7",                ColorRGBA(1.f, 0.85f, 0.1f, 1.f),   "Seven", 50, false},
			{FONT_ICON_STAR,     ColorRGBA(1.f, 0.95f, 0.2f, 1.f),   "Star",  20, true},
			{FONT_ICON_HEART,    ColorRGBA(1.f, 0.25f, 0.3f, 1.f),   "Heart", 10, true},
			{FONT_ICON_DICE_SIX, ColorRGBA(0.3f, 0.65f, 1.f, 1.f),   "Dice",   5, true},
			{FONT_ICON_KEY,      ColorRGBA(0.45f, 0.9f, 0.45f, 1.f), "Key",    4, true},
			{FONT_ICON_BOMB,     ColorRGBA(0.75f, 0.75f, 0.8f, 1.f), "Bomb",   3, true},
		};

		const float DeltaTime = Client()->RenderFrameTime();
		const int aMults[5] = {1, 2, 5, 10, 25};
		const int MulIdx = s_Casino.m_MultiplierIdx;
		const int CurMult = aMults[MulIdx];
		const int BaseBet = 10;
		const int ActualBet = BaseBet * CurMult;
		// x1→3, x2→4, x5→5, x10→6, x25→7
		const int ActualReels = minimum(2 + MulIdx + 1, 7);
		// streak bonus: +1% chance per losing spin, resets on win
		const float StreakBonus = s_Casino.m_SpinStreak * 0.01f;
		auto CalcWinChance = [&](int Mult) -> float { return std::clamp(0.22f - Mult * 0.015f + StreakBonus, 0.02f, 0.55f); };

		if(s_Casino.m_Spinning)
		{
			s_Casino.m_SpinTimer += DeltaTime;
			const float ScrollSpeed = 9.f;
			for(int i = 0; i < ActualReels; ++i)
			{
				const float StopTime = 0.7f + i * 0.35f;
				if(!s_Casino.m_aLocked[i])
				{
					s_Casino.m_aScrollY[i] += ScrollSpeed * DeltaTime;
					while(s_Casino.m_aScrollY[i] >= 1.f)
					{
						s_Casino.m_aScrollY[i] -= 1.f;
						s_Casino.m_aScrollSym[i] = (s_Casino.m_aScrollSym[i] + 1) % 6;
					}
					if(s_Casino.m_SpinTimer >= StopTime)
					{
						s_Casino.m_aLocked[i] = true;
						s_Casino.m_aScrollY[i] = 0.f;
						// center slot = (ScrollSym+1)%6, so align to target symbol
						s_Casino.m_aScrollSym[i] = (s_Casino.m_aSymbols[i] + 5) % 6;
					}
				}
			}
			const float LastStop = 0.7f + (ActualReels - 1) * 0.35f;
			if(s_Casino.m_SpinTimer >= LastStop)
			{
				s_Casino.m_Spinning = false;
				s_Casino.m_ShowResult = true;
				s_Casino.m_ResultTimer = 3.f;
				bool AllSame = true;
				for(int i = 1; i < ActualReels; ++i)
					if(s_Casino.m_aSymbols[i] != s_Casino.m_aSymbols[0]) { AllSame = false; break; }
				if(AllSame)
				{
					s_Casino.m_Won = true;
					s_Casino.m_WinAmount = ActualBet * s_aSym[s_Casino.m_aSymbols[0]].m_Payout;
					g_Config.m_BcCasinoBalance += s_Casino.m_WinAmount;
					s_Casino.m_SpinStreak = 0;
				}
				else
				{
					s_Casino.m_Won = false;
					s_Casino.m_WinAmount = 0;
					s_Casino.m_SpinStreak = minimum(s_Casino.m_SpinStreak + 1, 50);
				}
			}
		}
		if(s_Casino.m_ShowResult)
		{ s_Casino.m_ResultTimer -= DeltaTime; if(s_Casino.m_ResultTimer <= 0.f) s_Casino.m_ShowResult = false; }

		CUIRect Area = GameContent;

		CUIRect TopBar;
		Area.HSplitTop(34.f, &TopBar, &Area);
		Area.HSplitTop(MARGIN_SMALL, nullptr, &Area);
		TopBar.Draw(ColorRGBA(0.f, 0.f, 0.f, 0.35f), IGraphics::CORNER_ALL, 6.f);
		CUIRect TopInner; TopBar.Margin(4.f, &TopInner);
		CUIRect BalRect, ClaimRect;
		TopInner.VSplitRight(185.f, &BalRect, &ClaimRect);
		ClaimRect.VSplitLeft(MARGIN_SMALL, nullptr, &ClaimRect);

		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Balance:  $%d", g_Config.m_BcCasinoBalance);
		TextRender()->TextColor(ColorRGBA(0.75f, 1.f, 0.55f, 1.f));
		Ui()->DoLabel(&BalRect, aBuf, FONT_SIZE, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		const int64_t Now = (int64_t)time_timestamp();
		const int64_t CooldownSec = 60;
		const int64_t Elapsed = Now - (int64_t)g_Config.m_BcCasinoLastClaim;
		const bool CanClaim = Elapsed >= CooldownSec;
		if(CanClaim)
		{
			if(DoButton_Menu(&s_ClaimBtn, TCLocalize("Get $200"), 0, &ClaimRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.f, 0.f, ColorRGBA(0.2f, 0.65f, 0.2f, 0.9f)))
			{ g_Config.m_BcCasinoBalance += 200; g_Config.m_BcCasinoLastClaim = (int)Now; }
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "Get $200  (%ds)", (int)(CooldownSec - Elapsed));
			DoButton_Menu(&s_ClaimBtn, aBuf, -1, &ClaimRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.f, 0.f, ColorRGBA(0.3f, 0.3f, 0.3f, 0.85f));
		}
		// Multiplier row
		CUIRect MulRow;
		Area.HSplitTop(LINE_SIZE + 4.f, &MulRow, &Area);
		Area.HSplitTop(MARGIN_SMALL, nullptr, &Area);
		CUIRect MulLabel;
		MulRow.VSplitLeft(80.f, &MulLabel, &MulRow);
		Ui()->DoLabel(&MulLabel, TCLocalize("Multiplier:"), FONT_SIZE, TEXTALIGN_ML);
		const float MulBtnW = MulRow.w / 5.f;
		for(int m = 0; m < 5; ++m)
		{
			CUIRect MB;
			MulRow.VSplitLeft(MulBtnW, &MB, &MulRow);
			const int Corners = m == 0 ? IGraphics::CORNER_L : (m == 4 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
			str_format(aBuf, sizeof(aBuf), "x%d", aMults[m]);
			if(DoButton_Menu(&s_aMulBtn[m], aBuf, s_Casino.m_MultiplierIdx == m ? 1 : 0, &MB, BUTTONFLAG_LEFT, nullptr, Corners, 4.f, 0.f, s_Casino.m_MultiplierIdx == m ? ColorRGBA(0.22f, 0.55f, 0.9f, 0.9f) : ColorRGBA(1.f,1.f,1.f,0.18f)))
				if(!s_Casino.m_Spinning)
					s_Casino.m_MultiplierIdx = m;
		}

		// Bet info line
		CUIRect BetInfo;
		Area.HSplitTop(LINE_SIZE, &BetInfo, &Area);
		Area.HSplitTop(MARGIN_SMALL, nullptr, &Area);
		{
			const int MaxWin = ActualBet * s_aSym[0].m_Payout;
			str_format(aBuf, sizeof(aBuf), "Chance: %.0f%%   Max win: $%d%s",
				CalcWinChance(CurMult) * 100.f,
				MaxWin,
				s_Casino.m_SpinStreak > 0 ? "  [streak!]" : "");
		}
		TextRender()->TextColor(ColorRGBA(0.85f, 0.85f, 0.85f, 0.8f));
		Ui()->DoLabel(&BetInfo, aBuf, FONT_SIZE * 0.88f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		// Reels area
		CUIRect ReelArea;
		Area.HSplitTop(Area.h - 40.f - MARGIN_SMALL * 2.f, &ReelArea, &Area);
		ReelArea.Draw(ColorRGBA(0.f, 0.f, 0.f, 0.45f), IGraphics::CORNER_ALL, 10.f);

		const float ReelW = ReelArea.w / ActualReels;
		const float SymFontSize = 28.f; // fixed size — prevents glyph cache rebuilds on reel count change
		for(int i = 0; i < ActualReels; ++i)
		{
			CUIRect Reel;
			Reel.x = ReelArea.x + i * ReelW + 4.f;
			Reel.y = ReelArea.y + 4.f;
			Reel.w = ReelW - 8.f;
			Reel.h = ReelArea.h - 8.f;

			const bool Spinning = s_Casino.m_Spinning && !s_Casino.m_aLocked[i];
			ColorRGBA ReelBg = Spinning
				? ColorRGBA(0.14f, 0.14f, 0.18f, 0.9f)
				: ColorRGBA(0.10f, 0.10f, 0.14f, 0.9f);
			Reel.Draw(ReelBg, IGraphics::CORNER_ALL, 8.f);

			Ui()->ClipEnable(&Reel);

			auto RenderSym = [&](int SymIdx, float OffsetY) {
				const SSymbol &Sym = s_aSym[SymIdx % 6];
				CUIRect R = Reel;
				R.y += OffsetY;
				if(Sym.m_IsIcon)
				{
					TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
					TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
				}
				const float Alpha = Spinning ? 0.75f : 1.f;
				TextRender()->TextColor(ColorRGBA(Sym.m_Color.r, Sym.m_Color.g, Sym.m_Color.b, Alpha));
				Ui()->DoLabel(&R, Sym.m_pIcon, SymFontSize, TEXTALIGN_MC);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
				if(Sym.m_IsIcon)
				{
					TextRender()->SetRenderFlags(0);
					TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
				}
			};

			if(Spinning)
			{
				// top symbol scrolling down, next symbol below
				const float Off = s_Casino.m_aScrollY[i] * Reel.h;
				RenderSym(s_Casino.m_aScrollSym[i], -Reel.h + Off);
				RenderSym((s_Casino.m_aScrollSym[i] + 1) % 6, Off);
				RenderSym((s_Casino.m_aScrollSym[i] + 2) % 6, Reel.h + Off);
			}
			else
			{
				RenderSym(s_Casino.m_aSymbols[i], 0.f);
				// Locked indicator during spin
				if(s_Casino.m_Spinning && s_Casino.m_aLocked[i])
				{
					CUIRect LockedLabel;
					Reel.HSplitBottom(18.f, nullptr, &LockedLabel);
					TextRender()->TextColor(ColorRGBA(0.5f, 0.5f, 0.5f, 0.7f));
					Ui()->DoLabel(&LockedLabel, TCLocalize("Locked"), FONT_SIZE * 0.75f, TEXTALIGN_MC);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
				}
			}

			Ui()->ClipDisable();

			// Win highlight
			if(s_Casino.m_ShowResult && s_Casino.m_Won)
			{
				const float Alpha = sinf(AnimTime * 10.f) * 0.15f + 0.25f;
				Reel.Draw(ColorRGBA(1.f, 0.85f, 0.1f, Alpha), IGraphics::CORNER_ALL, 8.f);
			}
		}

		// Result overlay
		if(s_Casino.m_ShowResult)
		{
			CUIRect ResLabel = ReelArea;
			ResLabel.y = ReelArea.y + ReelArea.h * 0.72f;
			ResLabel.h = 26.f;
			if(s_Casino.m_Won)
			{
				str_format(aBuf, sizeof(aBuf), "+ $%d", s_Casino.m_WinAmount);
				TextRender()->TextColor(ColorRGBA(0.3f, 1.f, 0.3f, 1.f));
			}
			else
			{
				str_format(aBuf, sizeof(aBuf), "- $%d", ActualBet);
				TextRender()->TextColor(ColorRGBA(1.f, 0.3f, 0.3f, 1.f));
			}
			Ui()->DoLabel(&ResLabel, aBuf, 18.f, TEXTALIGN_MC);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}

		// Spin button
		Area.HSplitTop(MARGIN_SMALL, nullptr, &Area);
		CUIRect SpinBtn;
		Area.HSplitTop(34.f, &SpinBtn, &Area);
		const bool NotEnoughFunds = g_Config.m_BcCasinoBalance < ActualBet;
		if(s_Casino.m_Spinning || NotEnoughFunds)
		{
			const char *pLabel = NotEnoughFunds ? TCLocalize("Not enough funds") : TCLocalize("Spinning...");
			DoButton_Menu(&s_SpinBtn, pLabel, -1, &SpinBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 6.f, 0.f, ColorRGBA(0.3f,0.3f,0.3f,0.85f));
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "%s  (Bet $%d)", TCLocalize("SPIN"), ActualBet);
			if(DoButton_Menu(&s_SpinBtn, aBuf, 0, &SpinBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 6.f, 0.f, ColorRGBA(0.7f, 0.25f, 0.25f, 0.92f)) ||
				Input()->KeyPress(KEY_SPACE))
			{
				g_Config.m_BcCasinoBalance -= ActualBet;
				s_Casino.m_Spinning = true;
				s_Casino.m_SpinTimer = 0.f;
				s_Casino.m_ShowResult = false;
				for(int i = 0; i < ActualReels; ++i)
				{
					s_Casino.m_aLocked[i] = false;
					s_Casino.m_aScrollY[i] = 0.f;
					s_Casino.m_aScrollSym[i] = s_Casino.m_aSymbols[i];
					const float WinChance = CalcWinChance(CurMult);
					if(i > 0 && (rand() % 1000) < (int)(WinChance * 1000.f))
						s_Casino.m_aSymbols[i] = s_Casino.m_aSymbols[0];
					else
						s_Casino.m_aSymbols[i] = rand() % 6;
				}
			}
		}
	}
}
