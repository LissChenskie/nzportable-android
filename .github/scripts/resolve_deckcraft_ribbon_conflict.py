from pathlib import Path
import sys

path = Path(sys.argv[1] if len(sys.argv) > 1 else "deckcraft/crates/ui-egui/src/ribbon.rs")
source = path.read_text(encoding="utf-8")


def replace_once(old: str, new: str, label: str) -> None:
    global source
    count = source.count(old)
    if count != 1:
        raise SystemExit(f"Expected exactly one {label} anchor, found {count}.")
    source = source.replace(old, new, 1)


replace_once(
    "    let t = Tokens::get(ui.ctx());\n",
    "    let t = Tokens::get(ui.ctx());\n    let language = app.ui.interface_language;\n",
    "ribbon language context",
)
replace_once(
    '            let w = ui.fonts_mut(|f| f.layout_no_wrap("File".to_string(), theme::font(13.0), t.text).size().x) + 18.0;\n',
    '            let file = language.text("File");\n            let w = ui.fonts_mut(|f| f.layout_no_wrap(file.to_string(), theme::font(13.0), t.text).size().x) + 18.0;\n',
    "File tab width",
)
replace_once(
    '            resp.widget_info(|| egui::WidgetInfo::labeled(egui::Role::Button, true, "File"));',
    '            resp.widget_info(|| egui::WidgetInfo::labeled(egui::Role::Button, true, file));',
    "File accessibility label",
)
replace_once(
    '            ui.painter().text(r.center(), Align2::CENTER_CENTER, "File", theme::font(13.0), t.text);',
    '            ui.painter().text(r.center(), Align2::CENTER_CENTER, file, theme::font(13.0), t.text);',
    "File tab caption",
)
replace_once(
    '            if resp.on_hover_text("File (Alt+F)").clicked() {',
    '            if resp.on_hover_text(language.localized("File (Alt+F)")).clicked() {',
    "File tab tooltip",
)
replace_once(
    "            let label = tab_label(name);",
    "            let label = language.text(tab_label(name));",
    "contextual tab caption",
)
replace_once(
    '    let resp = ui.interact(rect, ui.scope_id().with(glyph), Sense::click()).on_hover_text(hint);\n    resp.widget_info(|| egui::WidgetInfo::labeled(egui::Role::Button, true, hint));',
    '    let hint = crate::i18n::context_text(ui.ctx(), hint);\n    let resp = ui.interact(rect, ui.scope_id().with(glyph), Sense::click()).on_hover_text(&hint);\n    resp.widget_info(|| egui::WidgetInfo::labeled(egui::Role::Button, true, &hint));',
    "ribbon overflow tooltip",
)

path.write_text(source, encoding="utf-8")
print(f"Resolved Russian UI labels in {path}.")
