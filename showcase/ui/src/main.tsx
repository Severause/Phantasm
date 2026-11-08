import { createRoot } from 'react-dom/client';
import '@fontsource/source-sans-3/400.css';
import '@fontsource/source-sans-3/600.css';
import '@fontsource/source-sans-3/700.css';
import './styles/tokens.css';
import './styles/app.css';
import { App } from './App';
import { initScale } from './lib/scale';

initScale();

createRoot(document.getElementById('root')!).render(<App />);
