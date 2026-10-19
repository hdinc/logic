-- counts rising clock edges while en is 1, rst (synchronous) clears it
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity counter is
    generic (
        WIDTH : positive := 8
    );
    port (
        clk, rst, en : in  std_logic;
        q            : out std_logic_vector(WIDTH - 1 downto 0)
    );
end entity;

architecture rtl of counter is
    signal c : unsigned(WIDTH - 1 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                c <= (others => '0');
            elsif en = '1' then
                c <= c + 1;
            end if;
        end if;
    end process;
    q <= std_logic_vector(c);
end architecture;
