import { Moon, Sun } from 'lucide-react'

import { Button } from '@/components/ui/button'

type ThemeToggleProps = {
  theme: 'light' | 'dark'
  onToggle: () => void
}

export function ThemeToggle({ theme, onToggle }: ThemeToggleProps) {
  const isDark = theme === 'dark'

  return (
    <Button
      variant="ghost"
      size="icon"
      onClick={onToggle}
      aria-label={isDark ? 'Skift til lyst tema' : 'Skift til mørkt tema'}
    >
      {isDark ? <Sun /> : <Moon />}
    </Button>
  )
}
