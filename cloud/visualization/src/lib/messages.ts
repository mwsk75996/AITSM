export const EYEBROW_MESSAGES = [
  'Live fra feltet!',
  'Varmt lige nu, eller er det bare mig?',
  'Sensorerne sover aldrig.',
  'Friskt fra Thingy:91 X!',
  'Held og lykke med at holde batteriet i live.',
  'Data så friske, de damper stadig.',
  '100% flere grader end forventet, måske.',
  'Ingen mennesker blev involveret i denne måling.',
  'Straight outta QuestDB.',
  'Bip. Bop. Måling modtaget.',
  'Kold kaffe, varme sensorer.',
  'Det her opdaterer sig selv - du behøver ikke.',
  'Endnu ikke sponsoreret af et termometer.',
  'Live og direkte, ligesom vejret.',
  'Batteriniveau: bekymrende optimistisk.',
  'Reload for endnu en tilfældig hilsen!',
  'Sensorerne rapporterer, vi bare videreformidler.',
  'Ingen skyer her, kun sky-data.',
  'Måler verden, ét device ad gangen.',
  'Held og lykke, må dine grader være stabile.',
]

export function pickRandomEyebrowMessage(): string {
  return EYEBROW_MESSAGES[Math.floor(Math.random() * EYEBROW_MESSAGES.length)]
}
