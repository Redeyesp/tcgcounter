#include "App.h"
#include "Screens.h"
#include "InputEvents.h"
#include "Log.h"

static Screen s_drawnScreen = SCREEN_COUNT;  // nothing drawn yet
static bool   s_fullRedraw  = true;

static const ScreenModule& moduleFor(Screen s) {
  switch (s) {
    case SCREEN_COMMANDER: return CommanderScreen;
    case SCREEN_COMMANDER_SETUP: return CommanderSetupScreen;
    case SCREEN_STANDARD:        return CommanderScreen;  // the 2-player Commander table, life only
    case SCREEN_STANDARD_SETUP:  return StandardSetupScreen;
    case SCREEN_KINGDOMS:        return KingdomsScreen;
    case SCREEN_KINGDOMS_SETUP:  return KingdomsSetupScreen;
    case SCREEN_RIFTBOUND: return RiftboundScreen;
    case SCREEN_LORCANA:   return LorcanaScreen;
    case SCREEN_RIFTBOUND_SETUP: return RiftboundSetupScreen;
    case SCREEN_LORCANA_SETUP:   return LorcanaSetupScreen;
    case SCREEN_POKEMON:         return PokemonScreen;
    case SCREEN_POKEMON_SETUP:   return PokemonSetupScreen;
    case SCREEN_DIGIMON:         return DigimonScreen;
    case SCREEN_DIGIMON_SETUP:   return DigimonSetupScreen;
    case SCREEN_DICE:      return DiceScreen;
    case SCREEN_HOME:
    default:               return HomeScreen;
  }
}

void requestFullRedraw() { s_fullRedraw = true; }

void setupApplication() {
  clearInput();
  moduleFor(g_state.screen).onEnter();
  requestFullRedraw();
  LOGF("[app] start on %s\n", moduleFor(g_state.screen).name);
}

void goToScreen(Screen s) {
  if (s >= SCREEN_COUNT || s == g_state.screen) return;
  LOGF("[app] %s -> %s\n", moduleFor(g_state.screen).name, moduleFor(s).name);
  g_state.screen = s;  // persisted automatically (last active screen)
  moduleFor(s).onEnter();
}

void updateApplication() {
  InputEvent e;
  while (popInput(e)) moduleFor(g_state.screen).handleInput(e);
  const ScreenModule& m = moduleFor(g_state.screen);
  if (m.tick) m.tick(millis());
}

void renderIfNeeded() {
  const bool full = s_fullRedraw || g_state.screen != s_drawnScreen;
  s_fullRedraw = false;
  s_drawnScreen = g_state.screen;
  moduleFor(g_state.screen).render(full);
}
