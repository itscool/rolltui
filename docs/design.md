# rolltui design rules

The screens that ship with rolltui (the window kinds, the editors, the menus and the studio) follow
these rules, and programs built on it should too. Each rule comes with a check you can apply to any
screen.

## 1. One screen per subject

The keys, a theme, the settings, a layout and a menu each have exactly one screen, and that screen
is where you both read and edit it. The help you open with F1 is the keys editor, so if something
there is wrong you can fix it on the spot.

*Check:* if a second screen shows the same data in another way, remove one of them.

## 2. Show the content first

The first thing on screen is the subject itself: every key and what it does, every colour role with
a sample, every setting with its value. Load, save, reset, generate and check are keys listed along
the bottom line, not items at the top of a menu.

*Check:* the first screen shouldn't need a keypress before it shows what the user came to change.

## 3. Keep it flat

Use one scrolling list grouped under headings. Only use nesting where the subject really nests (a
layout's tree, a menu's submenus), and show it as indentation in place, so the path to the
selected row stays visible.

*Check:* nothing you can edit should be more than one keypress away from the overview.

## 4. Edit in place

Enter edits the row where it is. A key row records the next chord pressed, a colour row takes typed
text and updates its swatch as you type, and a choice opens a dropdown on its row. The heading and
the neighbouring rows stay visible, and Escape cancels the edit, not the screen.

*Check:* editing a row shouldn't hide its neighbours or its heading.

## 5. Plain labels

A row says what the thing does, such as "the row above" or "move one word left". Action names like
`list.up` or `input.word_left` only appear where someone would type them: in a file, and in the
detail line of the row they belong to.

*Check:* no row label should be an identifier.

## 6. Show the effect while editing

Typing a colour changes the screen behind the editor as you type, and a new chord shows up in the
help line straight away. Enter keeps an edit, Escape drops it, and Ctrl-Z undoes it later.

*Check:* you shouldn't have to leave the editor to see what an edit did.

## 7. Every list can be filtered

Every list filters as you type, the same way everywhere: fuzzy, ignoring case and accents, with the
count in the footer. Help, keys, theme roles, settings and menus all work the same.

*Check:* any list longer than the screen should have a filter.

## 8. Consistent keys

Up and Down (or j and k with the vim keys) move, Enter edits or acts, Escape goes back one level and
only closes at the top level, `/` filters, and Ctrl-Z undoes. Each F-key opens the same thing in
every program: F1 the keys, F2 the menu, F4 the theme.

*Check:* a key shouldn't do different things on two screens that show the same kind of thing.

## 9. Show problems where they are

A chord bound to two actions, two colours too close to read, or a chord the terminal can't send is
marked on its own row as soon as it happens, along with what to do about it. There's no separate
list of problems to go and look at.

*Check:* the user should never have to open another screen to find out about a problem.

## 10. Save changes and make them reversible

Edits go to the user's own copy and are saved as they're made. The copy is labelled "modified", the
preset it came from stays unchanged and can still be picked, and one key switches back to it.

*Check:* no edit should be lost on quitting or overwrite a preset.

## 11. Work at 80 by 24

Every screen has to work at 80 by 24. Side panes turn into a drawer over the main pane, columns are
dropped from the right, and text is cut with an ellipsis at the end that matters least.

*Check:* at 80 by 24 nothing the user needs should be hidden, and nothing should overlap.
