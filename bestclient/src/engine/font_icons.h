#ifndef ENGINE_FONT_ICONS_H
#define ENGINE_FONT_ICONS_H

namespace FontIcon
{
	// Each font icon is named according to its official name in Font Awesome.
	// The constants are sorted in lexicographical order.

	inline const char *const ARROW_ROTATE_LEFT = "\uF0E2";
	inline const char *const ARROW_ROTATE_RIGHT = "\uF01E";
	inline const char *const ARROW_UP_RIGHT_FROM_SQUARE = "\uF08E";
	inline const char *const ARROWS_LEFT_RIGHT = "\uF337";
	inline const char *const ARROWS_ROTATE = "\uF021";
	inline const char *const ARROWS_UP_DOWN = "\uF07D";
	inline const char *const BACKWARD = "\uF04A";
	inline const char *const BACKWARD_FAST = "\uF049";
	inline const char *const BACKWARD_STEP = "\uF048";
	inline const char *const BAN = "\uF05E";
	inline const char *const BOOKMARK = "\uF02E";
	inline const char *const BORDER_ALL = "\uF84C";
	inline const char *const CAMERA = "\uF030";
	inline const char *const CHEVRON_DOWN = "\uF078";
	inline const char *const CHEVRON_LEFT = "\uF053";
	inline const char *const CHEVRON_RIGHT = "\uF054";
	inline const char *const CHEVRON_UP = "\uF077";
	inline const char *const CIRCLE = "\uF111";
	inline const char *const CIRCLE_CHEVRON_DOWN = "\uF13A";
	inline const char *const CIRCLE_PLAY = "\uF144";
	inline const char *const CLAPPERBOARD = "\uE131";
	inline const char *const COMMENT = "\uF075";
	inline const char *const COMMENT_SLASH = "\uF4B3";
	inline const char *const DICE_FIVE = "\uF523";
	inline const char *const DICE_FOUR = "\uF524";
	inline const char *const DICE_ONE = "\uF525";
	inline const char *const DICE_SIX = "\uF526";
	inline const char *const DICE_THREE = "\uF527";
	inline const char *const DICE_TWO = "\uF528";
	inline const char *const EARTH_AMERICAS = "\uF57D";
	inline const char *const ELLIPSIS = "\uF141";
	inline const char *const EYE = "\uF06E";
	inline const char *const EYE_DROPPER = "\uF1FB";
	inline const char *const EYE_SLASH = "\uF070";
	inline const char *const FILE = "\uF15B";
	inline const char *const FILM = "\uF008";
	inline const char *const FLAG_CHECKERED = "\uF11E";
	inline const char *const FOLDER = "\uF07B";
	inline const char *const FOLDER_OPEN = "\uF07C";
	inline const char *const FOLDER_TREE = "\uF802";
	inline const char *const FORWARD = "\uF04E";
	inline const char *const FORWARD_FAST = "\uF050";
	inline const char *const FORWARD_STEP = "\uF051";
	inline const char *const GEAR = "\uF013";
	inline const char *const HEADPHONES = "\uF025";
	inline const char *const HEART = "\uF004";
	inline const char *const HEART_CRACK = "\uF7A9";
	inline const char *const HOUSE = "\uF015";
	inline const char *const IMAGE = "\uF03E";
	inline const char *const INFO = "\uF129";
	inline const char *const KEY = "\uF084";
	inline const char *const KEYBOARD = "\u2328";
	inline const char *const LAYER_GROUP = "\uF5FD";
	inline const char *const LIST_UL = "\uF0CA";
	inline const char *const LOCK = "\uF023";
	inline const char *const MAGNIFYING_GLASS = "\uF002";
	inline const char *const MAP = "\uF279";
	inline const char *const MINUS = "-";
	inline const char *const MICROPHONE = "\uF130";
	inline const char *const MUSIC = "\uF001";
	inline const char *const NETWORK_WIRED = "\uF6FF";
	inline const char *const NEWSPAPER = "\uF1EA";
	inline const char *const PAUSE = "\uF04C";
	inline const char *const PEN_TO_SQUARE = "\uF044";
	inline const char *const PENCIL = "\uF303";
	inline const char *const PLAY = "\uF04B";
	inline const char *const PLUS = "+";
	inline const char *const POWER_OFF = "\uF011";
	inline const char *const QUESTION = "?";
	inline const char *const REDO = "\uF2F9";
	inline const char *const RIGHT_FROM_BRACKET = "\uF2F5";
	inline const char *const RIGHT_TO_BRACKET = "\uF2F6";
	inline const char *const SLASH = "\uF715";
	inline const char *const SORT_DOWN = "\uF0DD";
	inline const char *const SORT_UP = "\uF0DE";
	inline const char *const SQUARE_MINUS = "\uF146";
	inline const char *const SQUARE_PLUS = "\uF0FE";
	inline const char *const STAR = "\uF005";
	inline const char *const STOP = "\uF04D";
	inline const char *const TERMINAL = "\uF120";
	inline const char *const TRASH = "\uF1F8";
	inline const char *const TRIANGLE_EXCLAMATION = "\uF071";
	inline const char *const UNDO = "\uF2EA";
	inline const char *const USER = "\uF007";
	inline const char *const USERS = "\uF0C0";
	inline const char *const VIDEO = "\uF03D";
	inline const char *const XMARK = "\uF00D";

	// TClient
	inline const char *const ICON_USERS = "\xEF\x83\x80";
}

namespace FontIcons
{
	// Font Awesome icons used by various BestClient/DDNet UI code in this repo.
	// These are intentionally kept in a separate namespace to avoid colliding with upstream's `FontIcon::*`.
	// The constants are referenced via `using namespace FontIcons;` in many translation units.

	inline const char *const FONT_ICON_ARROWS_ROTATE = FontIcon::ARROWS_ROTATE;
	inline const char *const FONT_ICON_ARROW_ROTATE_RIGHT = FontIcon::ARROW_ROTATE_RIGHT;
	inline const char *const FONT_ICON_ARROW_UP_RIGHT_FROM_SQUARE = FontIcon::ARROW_UP_RIGHT_FROM_SQUARE;
	inline const char *const FONT_ICON_BACKWARD_STEP = FontIcon::BACKWARD_STEP;
	inline const char *const FONT_ICON_BOMB = "\uF1E2";
	inline const char *const FONT_ICON_BORDER_ALL = FontIcon::BORDER_ALL;
	inline const char *const FONT_ICON_CAT = "\uF6BE";
	inline const char *const FONT_ICON_CHESS_BISHOP = "\uF43A";
	inline const char *const FONT_ICON_CHESS_KING = "\uF43F";
	inline const char *const FONT_ICON_CHESS_KNIGHT = "\uF441";
	inline const char *const FONT_ICON_CHESS_PAWN = "\uF443";
	inline const char *const FONT_ICON_CHESS_QUEEN = "\uF445";
	inline const char *const FONT_ICON_CHESS_ROOK = "\uF447";
	inline const char *const FONT_ICON_CIRCLE = FontIcon::CIRCLE;
	inline const char *const FONT_ICON_CUBES = "\uF1B3";
	inline const char *const FONT_ICON_DICE_FIVE = FontIcon::DICE_FIVE;
	inline const char *const FONT_ICON_DICE_FOUR = FontIcon::DICE_FOUR;
	inline const char *const FONT_ICON_DICE_ONE = FontIcon::DICE_ONE;
	inline const char *const FONT_ICON_DICE_SIX = FontIcon::DICE_SIX;
	inline const char *const FONT_ICON_DICE_THREE = FontIcon::DICE_THREE;
	inline const char *const FONT_ICON_DICE_TWO = FontIcon::DICE_TWO;
	inline const char *const FONT_ICON_DOVE = "\uF4BA";
	inline const char *const FONT_ICON_DRAGON = "\uF6D5";
	inline const char *const FONT_ICON_FLAG_CHECKERED = FontIcon::FLAG_CHECKERED;
	inline const char *const FONT_ICON_FOLDER = FontIcon::FOLDER;
	inline const char *const FONT_ICON_FOLDER_TREE = FontIcon::FOLDER_TREE;
	inline const char *const FONT_ICON_FORWARD_STEP = FontIcon::FORWARD_STEP;
	inline const char *const FONT_ICON_GAMEPAD = "\uF11B";
	inline const char *const FONT_ICON_GEAR = FontIcon::GEAR;
	inline const char *const FONT_ICON_GHOST = "\uF6E2";
	inline const char *const FONT_ICON_HASHTAG = "\uF292";
	inline const char *const FONT_ICON_HEADPHONES = FontIcon::HEADPHONES;
	inline const char *const FONT_ICON_IMAGE = FontIcon::IMAGE;
	inline const char *const FONT_ICON_LAYER_GROUP = FontIcon::LAYER_GROUP;
	inline const char *const FONT_ICON_LIGHTBULB = "\uF0EB";
	inline const char *const FONT_ICON_MAGNIFYING_GLASS = FontIcon::MAGNIFYING_GLASS;
	inline const char *const FONT_ICON_MAP = FontIcon::MAP;
	inline const char *const FONT_ICON_MICROPHONE = FontIcon::MICROPHONE;
	inline const char *const FONT_ICON_NETWORK_WIRED = FontIcon::NETWORK_WIRED;
	inline const char *const FONT_ICON_PAUSE = FontIcon::PAUSE;
	inline const char *const FONT_ICON_PLAY = FontIcon::PLAY;
	inline const char *const FONT_ICON_QUESTION = FontIcon::QUESTION;
	inline const char *const FONT_ICON_SNAKE = "\uF716";
	inline const char *const FONT_ICON_TABLE_TENNIS_PADDLE_BALL = "\uF45D";
	inline const char *const FONT_ICON_TRIANGLE_EXCLAMATION = FontIcon::TRIANGLE_EXCLAMATION;
	inline const char *const FONT_ICON_USERS = FontIcon::USERS;
	inline const char *const FONT_ICON_XMARK = FontIcon::XMARK;
}

#endif
