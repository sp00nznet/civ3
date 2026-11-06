#pragma once
/* Hold the returned fd until exit; never unlink the lock file. Cooperating
 * native hosts share one lock per writable Conquests directory. */
int civ3_game_lock(const char *conquests);
