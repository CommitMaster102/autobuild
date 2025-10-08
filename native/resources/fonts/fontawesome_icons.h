#pragma once

// Font Awesome 6.x Icon Definitions
// This file contains commonly used Font Awesome icon Unicode values

// Status Icons
#define ICON_FA_CIRCLE_CHECK "\uf058"        // in circle (running/success)
#define ICON_FA_CIRCLE "\uf111"              // circle (stopped/inactive)
#define ICON_FA_CIRCLE_DOT "\uf192"          // filled circle (active/running)
#define ICON_FA_PLAY "\uf04b"                // play (start)
#define ICON_FA_PAUSE "\uf04c"               // pause
#define ICON_FA_STOP "\uf04d"                // stop
#define ICON_FA_SQUARE_CHECK "\uf14a"        // checked box
#define ICON_FA_SQUARE "\uf0c8"              // empty box

// Task Management Icons
#define ICON_FA_TASKS "\uf0ae"               // tasks list
#define ICON_FA_COG "\uf013"                 // settings/configure
#define ICON_FA_TRASH "\uf1f8"               // delete/remove
#define ICON_FA_EDIT "\uf044"                // edit
#define ICON_FA_SAVE "\uf0c7"                // save
#define ICON_FA_DOWNLOAD "\uf019"            // download
#define ICON_FA_UPLOAD "\uf093"              // upload

// Navigation Icons
#define ICON_FA_ARROW_LEFT "\uf060"          // back
#define ICON_FA_ARROW_RIGHT "\uf061"         // forward
#define ICON_FA_ARROW_UP "\uf062"            // up
#define ICON_FA_ARROW_DOWN "\uf063"          // down
#define ICON_FA_CHEVRON_LEFT "\uf053"        // previous
#define ICON_FA_CHEVRON_RIGHT "\uf054"       // next
#define ICON_FA_CHEVRON_UP "\uf077"          // up
#define ICON_FA_CHEVRON_DOWN "\uf078"        // down

// Undo/Redo Icons
#define ICON_FA_ROTATE_LEFT "\uf0e2"         // undo/rotate left
#define ICON_FA_ROTATE_RIGHT "\uf01e"        // redo/rotate right
#define ICON_FA_UNDO "\uf0e2"                // undo (alias)
#define ICON_FA_REDO "\uf01e"                // redo (alias)

// File/Folder Icons
#define ICON_FA_FOLDER "\uf07b"              // folder
#define ICON_FA_FOLDER_OPEN "\uf07c"         // open folder
#define ICON_FA_FILE "\uf15b"                // file
#define ICON_FA_FILE_TEXT "\uf15c"           // text file
#define ICON_FA_FILE_CODE "\uf1c9"           // code file
#define ICON_FA_FOLDER_TREE "\uf802"         // folder tree
#define ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE "\uf35d"  // open external

// System Icons
#define ICON_FA_REFRESH "\uf021"             // refresh/reload (original)
#define ICON_FA_REFRESH_ALT "\uf2f1"         // refresh/reload (alternative)
#define ICON_FA_REFRESH_ALT2 "\uf0e2"        // refresh/reload (another alternative)
#define ICON_FA_REFRESH_ALT3 "\uf2f9"        // refresh/reload (another alternative)
#define ICON_FA_SYNC "\uf2f1"                // sync (alternative)
#define ICON_FA_SPINNER "\uf110"             // loading spinner
#define ICON_FA_CLOCK "\uf017"               // time/clock
#define ICON_FA_CALENDAR "\uf133"            // calendar
#define ICON_FA_HOURGLASS "\uf254"           // hourglass

// Communication Icons
#define ICON_FA_INFO "\uf129"                // information
#define ICON_FA_EXCLAMATION "\uf12a"         // warning
#define ICON_FA_QUESTION "\uf128"            // question
#define ICON_FA_CHECK "\uf00c"               // check/success
#define ICON_FA_TIMES "\uf00d"               // close/error
#define ICON_FA_BAN "\uf05e"                 // ban/disable

// Docker/Container Icons
#define ICON_FA_CUBE "\uf1b2"                // container/cube
#define ICON_FA_CUBES "\uf1b3"               // containers
#define ICON_FA_SERVER "\uf233"              // server
#define ICON_FA_DATABASE "\uf1c0"            // database
#define ICON_FA_TERMINAL "\uf120"            // terminal

// Log/Output Icons
#define ICON_FA_LIST "\uf03a"                // list
#define ICON_FA_BARS "\uf0c9"                // menu bars
#define ICON_FA_SEARCH "\uf002"              // search
#define ICON_FA_FILTER "\uf0b0"              // filter
#define ICON_FA_SORT "\uf0dc"                // sort
#define ICON_FA_SORT_UP "\uf0de"             // sort up
#define ICON_FA_SORT_DOWN "\uf0dd"           // sort down

// Network/Connection Icons
#define ICON_FA_WIFI "\uf1eb"                // wifi
#define ICON_FA_GLOBE "\uf0ac"               // globe/internet
#define ICON_FA_LINK "\uf0c1"                // link
#define ICON_FA_UNLINK "\uf127"              // unlink
#define ICON_FA_EXTERNAL_LINK "\uf08e"       // external link

// Security Icons
#define ICON_FA_LOCK "\uf023"                // lock/secure
#define ICON_FA_UNLOCK "\uf09c"              // unlock
#define ICON_FA_KEY "\uf084"                 // key
#define ICON_FA_SHIELD "\uf132"              // shield/security

// Development Icons
#define ICON_FA_CODE "\uf121"                // code
#define ICON_FA_BUG "\uf188"                 // bug
#define ICON_FA_WRENCH "\uf0ad"              // tools
#define ICON_FA_HAMMER "\uf6e3"              // build
#define ICON_FA_ROCKET "\uf135"              // deploy/launch

// UI Icons
#define ICON_FA_EYE "\uf06e"                 // view/show
#define ICON_FA_EYE_SLASH "\uf070"           // hide
#define ICON_FA_PLUS "\uf067"                // add
#define ICON_FA_MINUS "\uf068"               // remove
#define ICON_FA_X "\uf00d"                   // close
#define ICON_FA_CHECK_MARK "\uf00c"          // check

// Window Management Icons
#define ICON_FA_WINDOW_MAXIMIZE "\uf31e"     // maximize
#define ICON_FA_WINDOW_RESTORE "\uf78c"      // restore
#define ICON_FA_WINDOW_MINIMIZE "\uf2d1"     // minimize window

// Common combinations for our app
#define ICON_FA_RUNNING ICON_FA_CIRCLE_DOT   // for running tasks
#define ICON_FA_STOPPED ICON_FA_CIRCLE       // for stopped tasks
#define ICON_FA_SUCCESS ICON_FA_CIRCLE_CHECK // for success
#define ICON_FA_ERROR ICON_FA_TIMES          // for error
#define ICON_FA_WARNING ICON_FA_EXCLAMATION  // for warning
#define ICON_FA_INFO_ICON ICON_FA_INFO       // for info
