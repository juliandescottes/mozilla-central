"use strict";

// Dragging the elements again after a reset should work
add_task(function* () {
  yield startCustomizing();
  let historyButton = document.getElementById("wrapper-history-panelmenu");
  let prefsButton = document.getElementById("wrapper-preferences-button");

  ok(historyButton && prefsButton, "Draggable elements should exist");
  simulateItemDrag(historyButton, prefsButton);
  yield gCustomizeMode.reset();
  ok(CustomizableUI.inDefaultState, "Should be back in default state");

  historyButton = document.getElementById("wrapper-history-panelmenu");
  prefsButton = document.getElementById("wrapper-preferences-button");
  ok(historyButton && prefsButton, "Draggable elements should exist");
  simulateItemDrag(historyButton, prefsButton);

  yield endCustomizing();
});

add_task(function* asyncCleanup() {
  yield resetCustomization();
});
