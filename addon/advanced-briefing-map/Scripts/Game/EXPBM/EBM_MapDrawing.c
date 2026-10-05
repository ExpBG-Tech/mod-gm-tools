// Read-only accessors over the vanilla map drawing tool; drawing behaviour is unchanged.
modded class MapLine
{
 void EBM_GetSegment(out float startX, out float startY, out float endX, out float endY)
 {
  startX = m_fStartPointX;
  startY = m_fStartPointY;
  endX = m_fEndPointX;
  endY = m_fEndPointY;
 }
}

modded class SCR_MapDrawingUI
{
 // Finished lines plus the line currently being drawn, as world x/y start and end pairs.
 void EBM_CollectLines(notnull array<float> outLines, int limit)
 {
  int lineCount = m_aLines.Count();
  for (int i = 0; i < lineCount; i++)
  {
   if (outLines.Count() >= limit * 4) return;
   MapLine line = m_aLines[i];
   if (!line) continue;
   bool drawing = m_bIsLineBeingDrawn && i == m_iLineID;
   if (!line.m_bIsLineDrawn && !drawing) continue;
   float startX, startY, endX, endY;
   line.EBM_GetSegment(startX, startY, endX, endY);
   outLines.Insert(startX);
   outLines.Insert(startY);
   outLines.Insert(endX);
   outLines.Insert(endY);
  }
 }
}
