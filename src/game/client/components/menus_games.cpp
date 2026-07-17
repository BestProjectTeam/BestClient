/* Copyright © 2026 BestProject Team */
#include "menus.h"

#include <base/math.h>
#include <base/system.h>

#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <engine/textrender.h>

#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
	constexpr float FONT_SIZE = 14.0f;
	constexpr float LINE_SIZE = 20.0f;
	constexpr float HEADLINE_FONT_SIZE = 20.0f;
	constexpr float MARGIN_SMALL = 5.0f;

	// Only the games that are actually reachable from the Fun tab's game picker.
	enum EFunGame
	{
		FUN_GAME_CASINO = 0,
		FUN_GAME_SNAKE,
		FUN_GAME_MINESWEEPER,
		FUN_GAME_CHESS,
		FUN_GAME_MEMORY,
		FUN_GAME_PONG,
		FUN_GAME_BRICK_BREAKER,
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
		case 'P': return FontIcon::CHESS_PAWN;
		case 'N': return FontIcon::CHESS_KNIGHT;
		case 'B': return FontIcon::CHESS_BISHOP;
		case 'R': return FontIcon::CHESS_ROOK;
		case 'Q': return FontIcon::CHESS_QUEEN;
		case 'K': return FontIcon::CHESS_KING;
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
		{"Casino", FontIcon::DICE_SIX, "Spin the reels, bet & win"},
		{"Snake", FontIcon::SNAKE, "Arrows/WASD, Space restart"},
		{"Minesweeper", FontIcon::BOMB, "LMB open, RMB flag, hover hints"},
		{"Chess", FontIcon::CHESS_KING, "Font Awesome chess pieces"},
		{"Memory", FontIcon::LAYER_GROUP, "Find all matching pairs"},
		{"Pong", FontIcon::TABLE_TENNIS_PADDLE_BALL, "W/S or Up/Down to move paddle"},
		{"Brick Breaker", FontIcon::BORDER_ALL, "Break all bricks with the ball"},
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

	// Horizontal top bar with square game selector buttons (icon above, name below)
	const float BtnGap = MARGIN_SMALL;
	const float BtnW = (MainView.w - BtnGap * (NUM_FUN_GAMES - 1)) / (float)NUM_FUN_GAMES;
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

		for(int i = 0; i < NUM_FUN_GAMES; ++i)
		{
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
			bool m_Initialized = false;
			bool m_Waiting = true;
			int m_BoardW = 22;
			int m_BoardH = 15;
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
			if(Candidate == s_Snake.m_Dir)
				return;
			if(Candidate == ivec2(-s_Snake.m_Dir.x, -s_Snake.m_Dir.y))
				return;
			s_Snake.m_QueuedDir = Candidate;
			s_Snake.m_HasQueuedDir = true;
		};

		if(!s_Snake.m_Initialized)
			ResetSnake(true);

		CUIRect TopBarSnake, BoardArea;
		GameContent.HSplitTop(LINE_SIZE * 1.2f, &TopBarSnake, &BoardArea);
		BoardArea.HSplitTop(MARGIN_SMALL, nullptr, &BoardArea);

		CUIRect ScoreLabel, BtnArea, RestartButton;
		TopBarSnake.VSplitLeft(250.0f, &ScoreLabel, &BtnArea);
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
			if(Next.x < 0 || Next.y < 0 || Next.x >= s_Snake.m_BoardW || Next.y >= s_Snake.m_BoardH)
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
			const float BaseSpeed = 7.0f;
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

		auto MinesIndex = [&]() {
			return [&](int X, int Y) {
				return Y * s_Mines.m_W + X;
			};
		};

		auto ResetMines = [&]() {
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

		CUIRect TopBarMines, BoardArea;
		GameContent.HSplitTop(LINE_SIZE * 1.2f, &TopBarMines, &BoardArea);
		BoardArea.HSplitTop(MARGIN_SMALL, nullptr, &BoardArea);

		CUIRect Stats, BtnArea, RestartButton;
		TopBarMines.VSplitLeft(280.0f, &Stats, &BtnArea);
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
						RenderIconLabel(Cell, FontIcon::BOMB, Cell.h * 0.68f, TEXTALIGN_MC);
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
					RenderIconLabel(Cell, FontIcon::FLAG_CHECKERED, Cell.h * 0.6f, TEXTALIGN_MC);
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

		CUIRect TopBarChess, BoardArea;
		GameContent.HSplitTop(LINE_SIZE * 1.2f, &TopBarChess, &BoardArea);
		BoardArea.HSplitTop(MARGIN_SMALL, nullptr, &BoardArea);

		CUIRect TurnLabel, BtnArea, RestartButton;
		TopBarChess.VSplitLeft(320.0f, &TurnLabel, &BtnArea);
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
			const bool BotTurn = !s_Chess.m_WhiteTurn;
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
				else if(s_Chess.m_SelectedX >= 0 && s_Chess.m_SelectedY >= 0 && IsValidMove(s_Chess.m_SelectedX, s_Chess.m_SelectedY, x, y))
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
	else if(s_SelectedGame == FUN_GAME_MEMORY)
	{
		struct SMemoryState
		{
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

		auto ResetMemory = [&]() {
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

		CUIRect TopBarMemory, BoardArea;
		GameContent.HSplitTop(LINE_SIZE * 1.2f, &TopBarMemory, &BoardArea);
		BoardArea.HSplitTop(MARGIN_SMALL, nullptr, &BoardArea);

		CUIRect StatsLabel, BtnArea, RestartButton;
		TopBarMemory.VSplitLeft(300.0f, &StatsLabel, &BtnArea);
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
	else if(s_SelectedGame == FUN_GAME_PONG)
	{
		struct SPongState
		{
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
			s_Pong.m_BallVel = vec2((DirectionSign >= 0 ? 1.0f : -1.0f) * 0.66f, Angle);
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

		CUIRect TopBarPong, ArenaArea;
		GameContent.HSplitTop(LINE_SIZE * 1.2f, &TopBarPong, &ArenaArea);
		ArenaArea.HSplitTop(MARGIN_SMALL, nullptr, &ArenaArea);

		CUIRect Stats, BtnArea, RestartButton;
		TopBarPong.VSplitLeft(280.0f, &Stats, &BtnArea);
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

			const float BotSpeed = 0.95f;
			const float TargetY = s_Pong.m_BallPos.y + s_Pong.m_BallVel.y * 0.08f;
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
	else if(s_SelectedGame == FUN_GAME_BRICK_BREAKER)
	{
		struct SBrickBreakerState
		{
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
		auto ResetBrickRound = [&]() {
			s_Brick.m_PaddleX = 0.5f;
			s_Brick.m_BallPos = vec2(s_Brick.m_PaddleX, 0.84f);
			s_Brick.m_BallVel = vec2(0.0f, 0.0f);
			s_Brick.m_BallLaunched = false;
		};
		auto ResetBrickBreaker = [&]() {
			s_Brick.m_Initialized = true;
			s_Brick.m_Lives = s_Brick.m_LivesStart;
			s_Brick.m_Score = 0;
			s_Brick.m_GameOver = false;
			s_Brick.m_Won = false;
			s_Brick.m_vBricks.assign((size_t)s_Brick.m_Cols * s_Brick.m_Rows, 1);
			s_Brick.m_RemainingBricks = (int)s_Brick.m_vBricks.size();
			s_Brick.m_LastTick = time_get();
			ResetBrickRound();
		};

		if(!s_Brick.m_Initialized)
			ResetBrickBreaker();

		CUIRect TopBarBrick, ArenaArea;
		GameContent.HSplitTop(LINE_SIZE * 1.2f, &TopBarBrick, &ArenaArea);
		ArenaArea.HSplitTop(MARGIN_SMALL, nullptr, &ArenaArea);

		CUIRect Stats, BtnArea, RestartButton;
		TopBarBrick.VSplitLeft(380.0f, &Stats, &BtnArea);
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
					s_Brick.m_BallVel = vec2(Side, -0.82f);
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

		struct SSymbol
		{
			const char *m_pIcon;
			ColorRGBA m_Color;
			const char *m_pName;
			int m_Payout;
			bool m_IsIcon;
		};
		static const SSymbol s_aSym[6] = {
			{"7", ColorRGBA(1.f, 0.85f, 0.1f, 1.f), "Seven", 50, false},
			{FontIcon::STAR, ColorRGBA(1.f, 0.95f, 0.2f, 1.f), "Star", 20, true},
			{FontIcon::HEART, ColorRGBA(1.f, 0.25f, 0.3f, 1.f), "Heart", 10, true},
			{FontIcon::DICE_SIX, ColorRGBA(0.3f, 0.65f, 1.f, 1.f), "Dice", 5, true},
			{FontIcon::KEY, ColorRGBA(0.45f, 0.9f, 0.45f, 1.f), "Key", 4, true},
			{FontIcon::BOMB, ColorRGBA(0.75f, 0.75f, 0.8f, 1.f), "Bomb", 3, true},
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
					if(s_Casino.m_aSymbols[i] != s_Casino.m_aSymbols[0])
					{
						AllSame = false;
						break;
					}
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
		{
			s_Casino.m_ResultTimer -= DeltaTime;
			if(s_Casino.m_ResultTimer <= 0.f)
				s_Casino.m_ShowResult = false;
		}

		CUIRect Area = GameContent;

		CUIRect TopBarCasino;
		Area.HSplitTop(34.f, &TopBarCasino, &Area);
		Area.HSplitTop(MARGIN_SMALL, nullptr, &Area);
		TopBarCasino.Draw(ColorRGBA(0.f, 0.f, 0.f, 0.35f), IGraphics::CORNER_ALL, 6.f);
		CUIRect TopInner;
		TopBarCasino.Margin(4.f, &TopInner);
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
			{
				g_Config.m_BcCasinoBalance += 200;
				g_Config.m_BcCasinoLastClaim = (int)Now;
			}
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
			if(DoButton_Menu(&s_aMulBtn[m], aBuf, s_Casino.m_MultiplierIdx == m ? 1 : 0, &MB, BUTTONFLAG_LEFT, nullptr, Corners, 4.f, 0.f, s_Casino.m_MultiplierIdx == m ? ColorRGBA(0.22f, 0.55f, 0.9f, 0.9f) : ColorRGBA(1.f, 1.f, 1.f, 0.18f)))
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
			DoButton_Menu(&s_SpinBtn, pLabel, -1, &SpinBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 6.f, 0.f, ColorRGBA(0.3f, 0.3f, 0.3f, 0.85f));
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
