// The site's one authored motion: each figure starts neutral and the product acts on it, once, as it comes into view.

export const reducedMotion = () => window.matchMedia("(prefers-reduced-motion: reduce)").matches;

const easeOutExpo = (t: number) => (t >= 1 ? 1 : 1 - Math.pow(2, -10 * t));

/**
 * calls draw(t) with t from 0 to 1 the first time the figure is a third in view. The figure is already showing
 * t = 0: an inline script set it neutral before it painted (Neutral.astro). Without that, it shows t = 1 and
 * nothing moves.
 */
export function drawIn(figure: HTMLElement, draw: (t: number) => void, duration = 1600, delay = 120) {
  if (!figure.hasAttribute("data-neutral")) return;
  const observer = new IntersectionObserver(
    (entries) => {
      if (!entries.some((e) => e.isIntersecting)) return;
      observer.disconnect();
      const start = performance.now() + delay;
      const step = (now: number) => {
        const t = Math.max(0, Math.min(1, (now - start) / duration));
        draw(easeOutExpo(t));
        if (t < 1) requestAnimationFrame(step);
        else figure.removeAttribute("data-neutral");
      };
      requestAnimationFrame(step);
    },
    { threshold: 0.33 },
  );
  observer.observe(figure);
}
