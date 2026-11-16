// tools/bench/ratatui — THE SCREENS tools/bench/rolltui_bench.c DRAWS, drawn the way a ratatui program draws them: a Terminal
// over the crossterm backend, its bytes counted instead of written.
//
//   ratbench W H SCENARIO FRAMES
//
// move, scroll and tick are rolltui_bench's. `move-sliced` and `scroll-sliced` hand the Table only the rows on screen, keeping
// the offset here: ratatui's best case, where plain `move` and `scroll` hand it every row each frame, as its Table takes them.
use ratatui::{
    backend::CrosstermBackend,
    layout::{Constraint, Rect},
    style::{Color, Modifier, Style},
    text::{Line, Span},
    widgets::{Paragraph, Row, Table, TableState},
    Terminal, TerminalOptions, Viewport,
};
use std::io::Write;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::time::Instant;

static WRITTEN: AtomicUsize = AtomicUsize::new(0);
/// What a terminal would be sent, counted and dropped.
struct Count;
impl Write for Count {
    fn write(&mut self, b: &[u8]) -> std::io::Result<usize> {
        WRITTEN.fetch_add(b.len(), Ordering::Relaxed);
        Ok(b.len())
    }
    fn flush(&mut self) -> std::io::Result<()> { Ok(()) }
}

fn main() {
    let a: Vec<String> = std::env::args().collect();
    let w: u16 = a[1].parse().unwrap();
    let h: u16 = a[2].parse().unwrap();
    let scenario = a[3].clone();
    let frames: usize = a[4].parse().unwrap();
    let backend = CrosstermBackend::new(std::io::BufWriter::with_capacity(1 << 20, Count));
    let mut term = Terminal::with_options(backend, TerminalOptions { viewport: Viewport::Fixed(Rect::new(0, 0, w, h)) }).unwrap();
    let n = 10_000usize;
    let rows: Vec<[String; 3]> = (0..n)
        .map(|i| [format!("file{i}.txt"), format!("{}K", (i * 37) % 1000), format!("2026-11-{:02} 12:{:02}", i % 28 + 1, i % 60)])
        .collect();
    let lines: Vec<String> = (0..h as usize * 4)
        .map(|i| format!("{:5}  the quick brown fox jumps over the lazy dog, line {i} of the page that does not move", i))
        .collect();
    let mut state = TableState::default();
    let body = h as usize - 1;
    let mut total = 0usize;
    let mut warm = true;
    let t0 = Instant::now();
    let mut t_start = t0;
    for f in 0..frames + 1 {
        if f == 1 {
            warm = false;
            t_start = Instant::now();
        }
        // "move": the cursor walks down and back inside the first screenful, so only two rows change.
        // "scroll": the cursor walks on past the screen, so every row moves each frame.
        // "tick": a screen of text that does not change, and a counter in its corner that does.
        let sliced = scenario.ends_with("-sliced");
        let base = scenario.trim_end_matches("-sliced");
        match base {
            "move" | "scroll" => {
                let sel = if base == "move" {
                    let p = f % (2 * (body - 1));
                    if p < body { p } else { 2 * (body - 1) - p }
                } else {
                    f + body
                };
                let sel = sel % n;
                // SLICED: only the rows on screen are handed over, the offset kept here: ratatui's best case.
                let top = if sel >= body { sel + 1 - body } else { 0 };
                let (first, shown) = if sliced { (top, body) } else { (0, n) };
                state.select(Some(if sliced { sel - top } else { sel }));
                if sliced { *state.offset_mut() = 0; }
                term.draw(|fr| {
                    let t = Table::new(
                        rows[first..(first + shown).min(n)].iter().map(|r| Row::new([r[0].as_str(), r[1].as_str(), r[2].as_str()])),
                        [Constraint::Fill(1), Constraint::Length(6), Constraint::Length(16)],
                    )
                    .header(Row::new(["Name", "Size", "Modified"]).style(Style::new().add_modifier(Modifier::BOLD)))
                    .style(Style::new().fg(Color::Rgb(220, 220, 220)).bg(Color::Rgb(24, 24, 28)))
                    .row_highlight_style(Style::new().fg(Color::Rgb(255, 255, 255)).bg(Color::Rgb(40, 60, 90)));
                    fr.render_stateful_widget(t, fr.area(), &mut state);
                })
                .unwrap();
            }
            _ => {
                term.draw(|fr| {
                    let area = fr.area();
                    let text: Vec<Line> = lines.iter().take(h as usize).enumerate().map(|(i, l)| {
                        let st = if i % 3 == 0 { Style::new().fg(Color::Rgb(130, 170, 255)) } else { Style::new().fg(Color::Rgb(220, 220, 220)) };
                        Line::from(Span::styled(l.as_str(), st))
                    }).collect();
                    fr.render_widget(Paragraph::new(text).style(Style::new().bg(Color::Rgb(24, 24, 28))), area);
                    let c = format!("{:>8}", f);
                    fr.render_widget(Paragraph::new(Span::styled(c, Style::new().fg(Color::Rgb(255, 200, 80)).add_modifier(Modifier::BOLD))),
                        Rect::new(area.width - 8, 0, 8, 1));
                })
                .unwrap();
            }
        }
        let written = WRITTEN.swap(0, Ordering::Relaxed);
        if !warm {
            total += written;
        }
    }
    let el = t_start.elapsed();
    println!("ratatui {scenario} {w}x{h}: {:.1} us/frame, {:.0} bytes/frame", el.as_secs_f64() * 1e6 / frames as f64, total as f64 / frames as f64);
}
