# Generates the timer categories. TimerCategories.inc defines the CategoryID
# constants and is included by Timer.h. TimerCategoryInfos.inc contains the
# names of all categories and is only included by Timer.cpp.
lldb_tablegen(TimerCategories.inc -gen-lldb-timer-categories
  SOURCE TimerCategories.td
  TARGET LLDBTimerCategoriesGen)

lldb_tablegen(TimerCategoryInfos.inc -gen-lldb-timer-category-infos
  SOURCE TimerCategories.td
  TARGET LLDBTimerCategoryInfosGen)

# Maps command names to their timer category. Only included by
# CommandObject.cpp.
lldb_tablegen(CommandTimerCategories.inc -gen-lldb-command-timer-categories
  SOURCE TimerCategories.td
  TARGET LLDBCommandTimerCategoriesGen)
