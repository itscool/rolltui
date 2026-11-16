# rolltui design principles

These are the rules every screen the library ships follows: the window kinds, the editors, the menus
and the studio. A host's own screens follow them too. Each rule has a test that any screen can be
held to.

## 1. The view you read is the view you edit

Each subject has one screen: the keys, a theme, the settings, a layout, a menu. The help a person
reads (F1) is the keys editor. A person who sees something wrong changes it where they see it.

*Test:* a second screen shows the same data another way. Remove one of them.

## 2. Show the content, not the commands

The first thing on the screen is the subject itself: every key with what it does, every colour role
with a sample drawn in it, every setting with its value. Load, save, reset, generate and check are
keys along the bottom line, not the top of a menu.

*Test:* the first screen needs a keypress before it shows anything a person came to change.

## 3. Flat and grouped

One scrolling list, grouped under headings. Depth is used only where the subject nests (a layout's
tree, a menu's submenus), and it is drawn as indentation in place, so the path to the selected row
stays on the screen.

*Test:* something a person edits is more than one keypress from the overview.

## 4. Edit in place

Enter edits the row where it is. A key row takes the next chord pressed. A colour row takes typed
text and its swatch changes as it is typed. A choice opens a dropdown on its row. The heading and
the rows around it stay visible. Escape cancels the edit, not the screen.

*Test:* editing a row hides the row's neighbours or its heading.

## 5. Words people use

A row says what the thing does: "the row above", "move one word left". An action's name
(`list.up`, `input.word_left`) appears only where a person writes it, in a file, and in the detail
line of the row that has it.

*Test:* a row's label is an identifier.

## 6. The effect shows while editing

A colour changes the screen behind the editor as it is typed. A new chord shows in the help line.
Enter keeps an edit, Escape drops it, and Ctrl-Z undoes it afterwards.

*Test:* a person has to leave the editor to see what an edit did.

## 7. Find by typing

Every list filters as it is typed into, the same way everywhere: fuzzy, folding case and accents,
with the count in the footer. The help, the keys, the theme roles, the settings and the menus all
behave alike.

*Test:* a list longer than the screen has no filter.

## 8. The same keys mean the same thing everywhere

Up and Down (or j and k in the vim keys) move. Enter edits or acts. Escape goes back one level, and
only the top level closes. `/` filters. Ctrl-Z undoes. Each F-key opens the same thing in every
program: F1 the keys, F2 the menu, F4 the theme.

*Test:* a key does something different in two screens that show the same kind of thing.

## 9. A problem shows where it is

A chord bound to two actions, a pair of colours too close to read, a chord the terminal cannot send:
each is marked on its own row as soon as it exists, with what to do about it. There is no separate
screen of problems to visit.

*Test:* a person learns of a problem only by opening another screen.

## 10. Changes are kept, visible and reversible

An edit goes to the person's own copy and is saved as it is made. The copy is labelled "modified",
the preset it came from stays as it was and can still be chosen, and one key goes back to it.

*Test:* an edit is lost on quitting, or overwrites a preset.

## 11. A small screen is a layout, not a failure

At 80 by 24 every screen still works. Side panes become a drawer over the main one, columns drop
from the right, and text is cut with an ellipsis at the end that says least.

*Test:* a screen at 80 by 24 hides something a person needs, or draws over itself.
