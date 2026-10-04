// Native Enforce fixture. Invoke Run() in an isolated indexed test project.
// NOT RUN: this source task has no native slot. Not part of the runtime addon.
class EXPG_EditorTest
{
 static bool Run()
 {
  EXPG_EditorTicket ticket = new EXPG_EditorTicket();
  ticket.Start(11, 100);
  if (ticket.Consume(10, 101)) return false; // An older browser cannot claim this request.
  if (!ticket.Consume(11, 101)) return false;
  if (ticket.Consume(11, 102)) return false; // Selection is single use.
  ticket.Start(12, 200);
  ticket.Cancel(11);
  if (!ticket.Matches(12, 201)) return false; // Stale cancellation leaves current picker alive.
  ticket.Cancel(12);
  if (ticket.Consume(12, 201)) return false;
  ticket.Start(13, 300);
  if (ticket.Consume(13, 420)) return false; // Expired selection never spawns.
  ticket.Start(14, 500);
  ticket.Start(15, 501);
  if (ticket.Consume(14, 502)) return false;
  if (!ticket.Consume(15, 502)) return false;
  return true;
 }
}
