-- counts nanoseconds up to 10, finishes at 11 ns
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity finish is
    port (
        t : out std_logic_vector(7 downto 0) := (others => '0')
    );
end entity;

architecture sim of finish is
begin
    process
    begin
        for i in 1 to 10 loop
            wait for 1 ns;
            t <= std_logic_vector(to_unsigned(i, 8));
        end loop;
        -- after the last assignment took effect
        wait for 1 ns;
        std.env.finish;
        wait;
    end process;
end architecture;
